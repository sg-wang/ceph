// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */


#ifndef __MDLOG_H
#define __MDLOG_H

#include "include/types.h"
#include "include/Context.h"

#include "common/Thread.h"
#include "common/Cond.h"

#include "LogSegment.h"

#include <list>

//#include <ext/hash_map>
//using __gnu_cxx::hash_mapset;

class Journaler;
class LogEvent;
class MDS;
class LogSegment;
class ESubtreeMap;

class Logger;

#include <map>
using std::map;

/*
namespace __gnu_cxx {
  template<> struct hash<LogEvent*> {
    size_t operator()(const LogEvent *p) const { 
      static hash<unsigned long> H;
      return H((unsigned long)p); 
    }
  };
}
*/

class MDLog {
 protected:
  MDS *mds;
  int num_events; // in events
  int max_events;

  int unflushed;

  bool capped;

  inode_t log_inode;
  Journaler *journaler;

  Logger *logger;


  // -- replay --
  Cond replay_cond;

  class ReplayThread : public Thread {
    MDLog *log;
  public:
    ReplayThread(MDLog *l) : log(l) {}
    void* entry() {
      log->_replay_thread();
      return 0;
    }
  } replay_thread;

  friend class ReplayThread;
  friend class C_MDL_Replay;

  list<Context*> waitfor_replay;

  void _replay();         // old way
  void _replay_thread();  // new way


  // -- segments --
  map<off_t,LogSegment*> segments;
  set<LogSegment*> trimming_segments;
  int num_segments;

  class C_MDL_WroteSubtreeMap : public Context {
    MDLog *mdlog;
    off_t off;
  public:
    C_MDL_WroteSubtreeMap(MDLog *l, off_t o) : mdlog(l), off(o) { }
    void finish(int r) {
      mdlog->_logged_subtree_map(off);
    }
  };
  void _logged_subtree_map(off_t off);


  // -- subtreemaps --
  off_t  last_subtree_map;   // offsets of last committed subtreemap.  constrains trimming.
  list<Context*> subtree_map_expire_waiters;
  bool writing_subtree_map;  // one is being written now

  friend class C_MDS_WroteImportMap;
  friend class MDCache;

  void init_journaler();
 public:
  off_t get_last_subtree_map_offset() { return last_subtree_map; }
  void add_subtree_map_expire_waiter(Context *c) {
    subtree_map_expire_waiters.push_back(c);
  }
  void take_subtree_map_expire_waiters(list<Context*>& ls) {
    ls.splice(ls.end(), subtree_map_expire_waiters);
  }



  // replay state
  map<inodeno_t, set<inodeno_t> >   pending_exports;



 public:
  MDLog(MDS *m) : mds(m),
		  num_events(0), max_events(g_conf.mds_log_max_len),
		  unflushed(0),
		  capped(false),
		  journaler(0),
		  logger(0),
		  replay_thread(this),
		  last_subtree_map(0),
		  writing_subtree_map(false) {
  }		  
  ~MDLog();


  void start_new_segment(Context *onsync=0);
  LogSegment *get_current_segment() { 
    return segments.empty() ? 0:segments.rbegin()->second; 
  }


  void flush_logger();

  void set_max_events(size_t max) { max_events = max; }
  size_t get_max_events() { return max_events; }
  size_t get_num_events() { return num_events; }
  size_t get_non_subtreemap_events() { return num_events - subtree_map_expire_waiters.size(); }

  off_t get_read_pos();
  off_t get_write_pos();
  bool empty() {
    return num_events == 0;
    //return get_read_pos() == get_write_pos();
  }

  bool is_capped() { return capped; }
  void cap() { 
    capped = true;
    list<Context*> ls;
    ls.swap(subtree_map_expire_waiters);
    finish_contexts(ls);
  }

  void submit_entry( LogEvent *e, Context *c = 0 );
  void wait_for_sync( Context *c );
  void flush();

  void trim();
  void _trimmed(LogSegment *ls);

  void reset();  // fresh, empty log! 
  void open(Context *onopen);
  void write_head(Context *onfinish);

  void replay(Context *onfinish);
};

#endif
