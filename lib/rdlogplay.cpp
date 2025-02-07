// rdlogplay.cpp
//
// Rivendell Log Playout Machine
//
//   (C) Copyright 2002-2023 Fred Gleason <fredg@paravelsystems.com>
//
//   This program is free software; you can redistribute it and/or modify
//   it under the terms of the GNU General Public License version 2 as
//   published by the Free Software Foundation.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public
//   License along with this program; if not, write to the Free Software
//   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include "rdapplication.h"
#include "rdconf.h"
#include "rddb.h"
#include "rddebug.h"
#include "rdescape_string.h"
#include "rdlog.h"
#include "rdlogplay.h"
#include "rdsvc.h"
#include "rdweb.h"

//
// Debug Settings
//
//#define SHOW_SLOTS
//#define SHOW_METER_SLOTS

RDLogPlay::RDLogPlay(int id,RDEventPlayer *player,bool enable_cue,QObject *parent)
  : RDLogModel(parent)
{
  //
  // Initialize Data Structures
  //
  play_log=NULL;
  play_id=id;
  play_event_player=player;
  play_onair_flag=false;
  play_segue_length=rda->airplayConf()->segueLength()+1;
  play_trans_length=rda->airplayConf()->transLength()+1;
  play_duck_volume_port1=0;
  play_duck_volume_port2=0;
  play_start_next=false;
  play_running=false;
  play_next_line=0;
  play_post_time=QTime();
  play_post_offset=-1;
  play_active_line=-1;
  play_active_trans=RDLogLine::Play;
  play_trans_line=-1;
  play_grace_line=-1;
  next_channel=0;
  play_timescaling_available=false;
  play_rescan_pos=0;
  play_refreshable=false;
  play_audition_preroll=rda->airplayConf()->auditionPreroll();
  play_line_counter=0;
  play_slot_quantity=1;
  for(int i=0;i<LOGPLAY_MAX_PLAYS;i++) {
    play_slot_id[i]=i;
  }
  for(int i=0;i<24;i++) {
    play_hours[i]=false;
  }

  //
  // PAD Server Connection
  //
  play_pad_socket=new RDUnixSocket(this);
  if(!play_pad_socket->connectToAbstract(RD_PAD_SOURCE_UNIX_ADDRESS)) {
    fprintf(stderr,"RDLogPlay: unable to connect to rdpadd\n");
  }

  //
  // CAE Connection
  //
  //  play_cae=new RDCae(rda->station(),rda->config(),parent);
  //  play_cae->connectHost();
  play_cae=rda->cae();

  for(int i=0;i<2;i++) {
    play_card[i]=0;
    play_port[i]=0;
  }
  for(int i=0;i<RD_MAX_CARDS;i++) {
    play_timescaling_supported[i]=false;
  }

  //
  // Play Decks
  //
  for(int i=0;i<RD_MAX_STREAMS;i++) {
    play_deck[i]=new RDPlayDeck(play_cae,0,this);
    play_deck_active[i]=false;
  }
  play_macro_running=false;
  play_refresh_pending=false;
  play_now_cartnum=0;
  play_next_cartnum=0;
  play_prevnow_cartnum=0;
  play_prevnext_cartnum=0;
  play_op_mode=RDAirPlayConf::Auto;

  //
  // Macro Cart Decks
  //
  play_macro_deck=new RDMacroEvent(rda->station()->address(),rda->ripc(),this);
  connect(play_macro_deck,SIGNAL(started()),this,SLOT(macroStartedData()));
  connect(play_macro_deck,SIGNAL(finished()),this,SLOT(macroFinishedData()));
  connect(play_macro_deck,SIGNAL(stopped()),this,SLOT(macroStoppedData()));

  //
  // CAE Signals
  //
  connect(play_cae,SIGNAL(timescalingSupported(int,bool)),
	  this,SLOT(timescalingSupportedData(int,bool)));

  //
  // RIPC Signals
  //
  connect(rda->ripc(),SIGNAL(onairFlagChanged(bool)),
	  this,SLOT(onairFlagChangedData(bool)));
  connect(rda->ripc(),SIGNAL(notificationReceived(RDNotification *)),
	  this,SLOT(notificationReceivedData(RDNotification *)));

  //
  // Audition Player
  //
  play_audition_line=-1;
  play_audition_head_played=false;
  if(enable_cue&&(rda->station()->cueCard()>=0)&&
     (rda->station()->cuePort()>=0)) {
    play_audition_player=
      new RDSimplePlayer(play_cae,rda->ripc(),rda->station()->cueCard(),
			 rda->station()->cuePort(),0,0);
    play_audition_player->playButton()->hide();
    play_audition_player->stopButton()->hide();
    connect(play_audition_player,SIGNAL(played()),
	    this,SLOT(auditionStartedData()));
    connect(play_audition_player,SIGNAL(stopped()),
	    this,SLOT(auditionStoppedData()));
  }
  else {
    play_audition_player=NULL;
  }

  //
  // Transition Timers
  //
  play_trans_timer=new QTimer(this);
  play_trans_timer->setSingleShot(true);
  connect(play_trans_timer,SIGNAL(timeout()),
	  this,SLOT(transTimerData()));
  play_grace_timer=new QTimer(this);
  play_grace_timer->setSingleShot(true);
  connect(play_grace_timer,SIGNAL(timeout()),
	  this,SLOT(graceTimerData()));
}


QString RDLogPlay::serviceName() const
{
  if(play_svc_name.isEmpty()) {
    return play_defaultsvc_name;
  }
  return play_svc_name;
}


void RDLogPlay::setServiceName(const QString &svcname)
{
  play_svc_name=svcname;
}


QString RDLogPlay::defaultServiceName() const
{
  return play_defaultsvc_name;
}


void RDLogPlay::setDefaultServiceName(const QString &svcname)
{
  play_defaultsvc_name=svcname;
}


int RDLogPlay::card(int channum) const
{
  return play_card[channum];
}


int RDLogPlay::port(int channum) const
{
  return play_port[channum];
}


bool RDLogPlay::channelsValid() const
{
  return (play_card[0]>=0)&&(play_card[1]>=0)&&
    (play_port[0]>=0)&&(play_port[1]>=0);
}


RDAirPlayConf::OpMode RDLogPlay::mode() const
{
  return play_op_mode;
}


void RDLogPlay::setOpMode(RDAirPlayConf::OpMode mode)
{
  if(mode==play_op_mode) {
    return;
  }
  play_op_mode=mode;
  UpdateStartTimes();
}


void RDLogPlay::setLogName(QString name)
{
  if(logName()!=name) {
    RDLogModel::setLogName(name);
    emit renamed();
    rda->airplayConf()->setCurrentLog(play_id,name);
  }
}


void RDLogPlay::setChannels(int cards[2],int ports[2],QString labels[2],
			  const QString start_rml[2],const QString stop_rml[2])
{
  for(int i=0;i<2;i++) {
    play_card[i]=cards[i];
    play_port[i]=ports[i];
    play_label[i]=labels[i];
    play_start_rml[i]=start_rml[i];
    play_stop_rml[i]=stop_rml[i];
    if(play_card[i]>=0) {
      play_cae->requestTimescale(play_card[i]);
    }
  }
}


void RDLogPlay::setSegueLength(int len)
{
  play_segue_length=len;
}


void RDLogPlay::setNowCart(unsigned cartnum)
{
  play_now_cartnum=cartnum;
}


void RDLogPlay::setNextCart(unsigned cartnum)
{
  play_next_cartnum=cartnum;
}


void RDLogPlay::auditionHead(int line)
{
  RDLogLine *logline=logLine(line);
  if((play_audition_player==NULL)||(logline==NULL)) {
    return;
  }
  if(play_audition_line>=0) {
    play_audition_player->stop();
  }
  play_audition_line=line;
  play_audition_head_played=true;
  play_audition_player->setCart(logline->cartNumber());
  play_audition_player->play();
}


void RDLogPlay::auditionTail(int line)
{
  RDLogLine *logline=logLine(line);
  if((play_audition_player==NULL)||(logline==NULL)) {
    return;
  }
  if(play_audition_line>=0) {
    play_audition_player->stop();
  }
  play_audition_line=line;
  play_audition_head_played=false;
  play_audition_player->setCart(logline->cartNumber());
  int start_pos=logline->endPoint()-play_audition_preroll;
  if(start_pos<0) {
    start_pos=0;
  }
  play_audition_player->play(start_pos-logline->startPoint());
}


void RDLogPlay::auditionStop()
{
  if(play_audition_player==NULL) {
    return;
  }
  if(play_audition_line>=0) {
    play_audition_player->stop();
  }
}


bool RDLogPlay::play(int line,RDLogLine::StartSource src,
		     int mport,bool skip_meta)
{
  QTime current_time=QTime::currentTime();
  RDLogLine *logline;
  if(!channelsValid()) {
    return false;
  }
  if((logline=logLine(line))==NULL) {
    return false;
  }
  if((runningEvents(NULL)>=LOGPLAY_MAX_PLAYS)&&
     (logline->status()!=RDLogLine::Paused)) {
    return false;
  }
  if(play_op_mode==RDAirPlayConf::Auto) {
    skip_meta=false;
  }

  //
  // Remove any intervening events
  //
  if(play_line_counter!=line) {
    int start_line=-1;
    int num_lines;
    for(int i=play_line_counter;i<line;i++) {
      if((logline=logLine(i))!=NULL) {
	if(logline->status()==RDLogLine::Scheduled) {
	  if(start_line==-1) {
	    start_line=i;
	    num_lines=1;
	  }
	  else {
	    num_lines++;
	  }
	}
      }
    }
  }
  //
  // Play it
  //
  if(!GetNextPlayable(&line,skip_meta,true)) {
    return false;
  }

  bool ret = false;
  if(play_segue_length==0) {
    ret = StartEvent(line,RDLogLine::Play,0,src,mport);
  } else {
    ret = StartEvent(line,RDLogLine::Segue,play_segue_length,src,mport);
  }
  SetTransTimer(current_time);
  return ret;
}


bool RDLogPlay::channelPlay(int mport)
{
  if(nextLine()<0) {
    return false;
  }
  return play(nextLine(),RDLogLine::StartChannel,mport,false);
}


bool RDLogPlay::stop(bool all,int port,int fade)
{
  RDLogLine *logline;
  int lines[TRANSPORT_QUANTITY];

  int n=runningEvents(lines);
  for(int i=0;i<n;i++) {
    if(all || port<1) { 
      stop(lines[i],fade);
      }
    else {
      logline=logLine(lines[i]);
      if((logline->cartType()==RDCart::Audio)
	 &&(RDPlayDeck *)logline->playDeck()!=NULL
	 &&logline->portName().toInt()==port ) {
	stop(lines[i],fade);
      }
    }
  }
  if(n>0) {
    return true;
  }
  return false;
}


bool RDLogPlay::stop(int line,int fade)
{
  RDLogLine *logline;

  if((logline=logLine(line))==NULL) {
    return false;
  }
  switch(logline->cartType()) {
  case RDCart::Audio:
    if(((RDPlayDeck *)logline->playDeck())==NULL) {
      return false;
    }
    ((RDPlayDeck *)logline->playDeck())->stop(fade,RD_FADE_DEPTH);
    return true;
    break;
    
  case RDCart::Macro:
    play_macro_deck->stop();
    break;
    
  case RDCart::All:
    break;
  }
  return false;
}


bool RDLogPlay::channelStop(int mport)
{
  RDLogLine *logline;
  int lines[TRANSPORT_QUANTITY];
  bool ret=false;

  int n=runningEvents(lines);
  for(int i=0;i<n;i++) {
    logline=logLine(lines[i]);
    if((logline->cartType()==RDCart::Audio)
       &&((RDPlayDeck *)logline->playDeck()!=NULL)) {
      if(((RDPlayDeck *)logline->playDeck())->channel()==mport) {
	stop(lines[i]);
	ret=true;
      }
    }
  }
  return ret;
}


bool RDLogPlay::pause(int line)
{
  RDLogLine *logline;
  
  if((logline=logLine(line))==NULL) {
    return false;
  }
  switch(logline->cartType()) {
  case RDCart::Audio:
    if(logline->playDeck()==NULL) {
      return false;
    }
    ((RDPlayDeck *)logline->playDeck())->pause();
    return true;
    break;

  case RDCart::Macro:
  case RDCart::All:
    break;
  }
  return false;
}


void RDLogPlay::duckVolume(int level,int fade,int mport)
{
  RDLogLine *logline;
  int lines[TRANSPORT_QUANTITY];

  if(mport==-1 || mport==1) {
	  play_duck_volume_port1=level;
  }
  if(mport==-1 || mport==2) {
	  play_duck_volume_port2=level;
  }
  int n=runningEvents(lines);
  for(int i=0;i<n;i++) {
    logline=logLine(lines[i]);
    if((logline->cartType()==RDCart::Audio) 
       && (RDPlayDeck *)logline->playDeck()!=NULL
       && ((logline->portName().toInt()==mport) || mport<1) ) {
      ((RDPlayDeck *)logline->playDeck())->duckVolume(level,fade);
    }
  }
}
		


void RDLogPlay::makeNext(int line,bool refresh_status)
{
  play_next_line=line;
  SendNowNext();
  SetTransTimer();
  UpdatePostPoint();
  emit nextEventChanged(line);
  ChangeTransport();
}


void RDLogPlay::load()
{
  int lines[TRANSPORT_QUANTITY];
  int running=0;

  play_duck_volume_port1=0;
  play_duck_volume_port2=0;
  
  //
  // Remove All Idle Events
  //
  if((running=runningEvents(lines))==0) {
    remove(0,lineCount(),false);
  }
  else {
    if(lines[running-1]<(lineCount()-1)) {
      remove(lines[running-1]+1,lineCount()-lines[running-1]-1,false);
    }
    for(int i=running-1;i>0;i--) {
      remove(lines[i-1]+1,lines[i]-lines[i-1]-1,false);
    }
    if(lines[0]!=0) {
      remove(0,lines[0],false);
    }
  }

  // Note that events left in the log are holdovers from a previous log.
  // Their IDs may clash with those of events in the log we will now load,
  // and it may be appropriate to ignore them in that case.
  for(int i = 0, ilim = lineCount(); i != ilim; ++i)
    logLine(i)->setHoldover(true);

  //
  // Load Events
  //
  RDLogModel::load();
  play_rescan_pos=0;
  if(play_timescaling_available) {
    for(int i=0;i<lineCount();i++) {
      logLine(i)->setTimescalingActive(logLine(i)->enforceLength());
    }
  }
  RefreshEvents(0,lineCount());
  RDLog *log=new RDLog(logName());
  play_svc_name=log->service();
  delete log;
  play_line_counter=0;
  play_next_line=0;
  //  UpdateStartTimes(0);
  UpdateStartTimes();
  emit reloaded();
  SetTransTimer();
  ChangeTransport();
  UpdatePostPoint();
  if((running>0)&&(lineCount()>running)) {
    makeNext(running);
  }

  //
  // Update Refreshability
  //
  if(play_log!=NULL) {
    delete play_log;
  }
  play_log=new RDLog(logName());
  play_link_datetime=play_log->linkDatetime();
  play_modified_datetime=play_log->modifiedDatetime();
  if(play_refreshable) {
    play_refreshable=false;
    emit refreshabilityChanged(play_refreshable);
  }
}


void RDLogPlay::append(const QString &log_name)
{
  int old_size=lineCount();

  if(lineCount()==0) {
    load();
    return;
  }

  RDLogModel::append(log_name);
  if(play_timescaling_available) {
    for(int i=old_size;i<lineCount();i++) {
      logLine(i)->setTimescalingActive(logLine(i)->enforceLength());
    }
  }
  RefreshEvents(old_size,lineCount()-old_size);
  UpdateStartTimes();
  emit reloaded();
  SetTransTimer();
  ChangeTransport();
  UpdatePostPoint();
}


bool RDLogPlay::refresh()            
{
  RDLogLine *s;
  RDLogLine *d;
  int prev_line;
  int prev_id;
  int next_line=-1;
  int next_id=-1;
  int current_id=-1;
  int lines[TRANSPORT_QUANTITY];
  int running;
  int first_non_holdover = 0;

  if(rda->config()->logLogRefresh()) {
    rda->syslog(rda->config()->logLogRefreshLevel(),"log refresh begins...");
    DumpToSyslog(rda->config()->logLogRefreshLevel(),"before refresh:");
  }

  if(play_macro_running) {
    play_refresh_pending=true;
    return true;
  }
  emit refreshStatusChanged(true);
  if((lineCount()==0)||(play_log==NULL)) {
    emit refreshStatusChanged(false);
    emit refreshabilityChanged(false);
    return true;
  }

  //
  // Load the Updated Log
  //
  RDLogModel *e=new RDLogModel();
  e->setLogName(logName());
  e->load();
  play_modified_datetime=play_log->modifiedDatetime();

  //
  // Get the Next Event
  //
  if(nextEvent()!=NULL) {   //End of the log?
    next_id=nextEvent()->id();
  }

  //
  // Get Running Events
  //
  running=runningEvents(lines);
  for(int i=0;i<running;i++) {
    if(lines[i]==play_next_line-1) {
      current_id=logLine(lines[i])->id();
    }
  }
  if(running>0 && next_id==-1) {                  //Last Event of Log Running?
    current_id=logLine(lines[running-1])->id();
  }

  //
  // Pass 1: Finished or Active Events
  //
  for(int i=0;i<lineCount();i++) {
    d=logLine(i);
    if(d->status()!=RDLogLine::Scheduled) {
      if((!d->isHoldover()) && (s=e->loglineById(d->id()))!=NULL) {
	// A holdover event may be finished or active,
	// but should not supress the addition of an
	// event with the same ID in this log.
	// Incrementing its ID here may flag it as an orphan
	// to be removed in step 4.
	s->incrementPass();
      }
      d->incrementPass();
    }
  }

  //
  // Pass 2: Purge Deleted Events
  //
  for(int i=lineCount()-1;i>=0;i--) {
    if(logLine(i)->pass()==0) {
      remove(i,1,false,true);
    }
  }

  // Find first non-holdover event, where start-of-log
  // new events should be added:
  for(int i=0;i<e->lineCount();i++) {
    if(logLine(i)!=NULL) {
      if(logLine(i)->isHoldover()) {
	++first_non_holdover;
      }
      else {
	break;
      }
    }
  }

  //
  // Pass 3: Add New Events
  //
  for(int i=0;i<e->lineCount();i++) {
    s=e->logLine(i);
    if(s->pass()==0) {
      if((prev_line=(i-1))<0) {  // First Event
	insert(first_non_holdover,s,false,true);
      }
      else {
	prev_id=e->logLine(prev_line)->id();   
	insert(lineById(prev_id, /*ignore_holdovers=*/true)+1,s,false,true);   
      }
    }
    else {
      loglineById(s->id(), /*ignore_holdovers=*/true)->incrementPass();
    }
  }

  //
  // Pass 4: Delete Orphaned Past Playouts
  //
  for(int i=lineCount()-1;i>=0;i--) {
    d=logLine(i);
    if((d->status()==RDLogLine::Finished)&&(d->pass()!=2)) {
      remove(i,1,false,true);
    }
  }

  //
  // Restore Next Event
  //
  if(current_id!=-1 && e->loglineById(current_id)!=NULL) {
    // Make Next after currently playing cart
    // The next event cannot have been a holdover,
    // as holdovers are always either active or finished.
    if((next_line=lineById(current_id, /*ignore_holdovers=*/true))>=0) {    
      makeNext(next_line+1,false);              
    }
  }
  else {
    if((next_line=lineById(next_id, /*ignore_holdovers=*/true))>=0) {     
     makeNext(next_line,false);               
    }
  } 
  
  //
  // Clean Up
  //
  delete e;
  for(int i=0;i<lineCount();i++) {
    logLine(i)->clearPass();
  }
  RefreshEvents(0,lineCount());
  UpdateStartTimes();
  UpdatePostPoint();
  SetTransTimer();
  ChangeTransport();
  emit reloaded();
  if(play_refreshable) {
    play_refreshable=false;
    emit refreshabilityChanged(play_refreshable);
  }

  emit refreshStatusChanged(false);

  if(rda->config()->logLogRefresh()) {
    DumpToSyslog(rda->config()->logLogRefreshLevel(),"after refresh:");
    rda->syslog(rda->config()->logLogRefreshLevel(),"...log refresh ends");
  }

  return true;
}


void RDLogPlay::save(int line)
{
  RDLogModel::save(rda->config(),line);
  if(play_log!=NULL) {
    delete play_log;
  }
  play_log=new RDLog(logName());
  QDateTime current_datetime=
    QDateTime(QDate::currentDate(),QTime::currentTime());
  play_log->setModifiedDatetime(current_datetime);
  play_modified_datetime=current_datetime;
  if(play_refreshable) {
    play_refreshable=false;
    emit refreshabilityChanged(play_refreshable);
  }
}


void RDLogPlay::clear()
{
  setLogName("");
  int start_line=0;
  play_duck_volume_port1=0;
  play_duck_volume_port2=0;
  while(ClearBlock(start_line++));
  play_svc_name=play_defaultsvc_name;
  play_rescan_pos=0;
  if(play_log!=NULL) {
    delete play_log;
    play_log=NULL;
  }
  SetTransTimer();
  UpdatePostPoint();
  if(play_refreshable) {
    play_refreshable=false;
    emit refreshabilityChanged(play_refreshable);
  }
  emit reloaded();
}


void RDLogPlay::insert(int line,int cartnum,RDLogLine::TransType next_type,
		     RDLogLine::TransType type)
{
  RDLogLine *logline;
  int lines[TRANSPORT_QUANTITY];
  RDPlayDeck *playdeck;
  int mod_line=-1;
  
  if(line<(lineCount()-1)) {
    if(logLine(line)->hasCustomTransition()) {
      mod_line=line+1;
    }
  }

  int running=runningEvents(lines);
  for(int i=0;i<running;i++) {
    if((logline=logLine(lines[i]))!=NULL) {
      if((playdeck=(RDPlayDeck *)logline->playDeck())!=NULL) {
	if((playdeck->id()>=0)&&
	   (playdeck->id()>=line)) {
	  playdeck->setId(playdeck->id()+1);
	}
      }
    }
  }
  if(play_macro_deck->line()>=0) {
    play_macro_deck->setLine(play_macro_deck->line()+1);
  }
  RDLogModel::insert(line,1);
  if((logline=logLine(line))==NULL) {
    RDLogModel::remove(line,1);
    return;
  }
  if(nextLine()>line) {
    makeNext(nextLine()+1);
  }
  if(nextLine()<0) {
    play_next_line=line;
  }
  logline->loadCart(cartnum,next_type,play_id,play_timescaling_available,type);
  logline->
    setTimescalingActive(play_timescaling_available&&logline->enforceLength());
  UpdateStartTimes();
  emit inserted(line);
  UpdatePostPoint();
  if(mod_line>=0) {
    emit modified(mod_line);
  }
  ChangeTransport();
  SetTransTimer();
  UpdatePostPoint();
}


void RDLogPlay::insert(int line,RDLogLine *l,bool update,
		       bool preserv_custom_transition)
{
  RDLogLine *logline;
  int lines[TRANSPORT_QUANTITY];
  RDPlayDeck *playdeck;
  int mod_line=-1;
  
  if(line<(lineCount()-1)) {
    if(logLine(line)->hasCustomTransition()) {
      mod_line=line+1;
    }
  }

  int running=runningEvents(lines);
  for(int i=0;i<running;i++) {
    if((logline=logLine(lines[i]))!=NULL) {
      if((playdeck=(RDPlayDeck *)logline->playDeck())!=NULL) {
	if((playdeck->id()>=0)&&
	   (playdeck->id()>=line)) {
	  playdeck->setId(playdeck->id()+1);
	}
      }
    }
  }
  if(play_macro_deck->line()>=0) {
    play_macro_deck->setLine(play_macro_deck->line()+1);
  }
  RDLogModel::insert(line,1,preserv_custom_transition);
  if((logline=logLine(line))==NULL) {
    RDLogModel::remove(line,1);
    return;
  }
  *logline=*l;
  if(nextLine()>line && update) {
    makeNext(nextLine()+1);
  }
  if(nextLine()<0) {
    play_next_line=line;
  }
  logline->
    setTimescalingActive(play_timescaling_available&&logline->enforceLength());
  if(update) {
    UpdateStartTimes();
    emit inserted(line);
    UpdatePostPoint();
    if(mod_line>=0) {
      emit modified(mod_line);
    }
    ChangeTransport();
    SetTransTimer();
    UpdatePostPoint();
  }
}


void RDLogPlay::remove(int line,int num_lines,bool update,
		       bool preserv_custom_transition)
{
  RDPlayDeck *playdeck;
  RDLogLine *logline;
  int mod_line=-1;

  if((num_lines==0)||(line<0)||(line>=lineCount())) {
    return;
  }
  if((line+num_lines)<(lineCount()-1)) {
    if(logLine(line+num_lines)->hasCustomTransition()) {
      mod_line=line;
    }
  }

  for(int i=line;i<(line+num_lines);i++) {
    if((logline=logLine(i))!=NULL) {
      if((playdeck=(RDPlayDeck *)logline->playDeck())!=NULL) {
	playdeck->clear();
	FreePlayDeck(playdeck);
      }
    }
  }

  int lines[TRANSPORT_QUANTITY];

  if(update) {
    emit removed(line,num_lines,false);
    }
  int running=runningEvents(lines);
  for(int i=0;i<running;i++) {
    if((logline=logLine(lines[i]))!=NULL) {
      if(logline->type()==RDLogLine::Cart) {
	playdeck=(RDPlayDeck *)logline->playDeck();
	if((playdeck->id()>=0)&&(playdeck->id()>line)) {
	  playdeck->setId(playdeck->id()-num_lines);
	}
      }
    }
  }
  if(play_macro_deck->line()>0) {
    play_macro_deck->setLine(play_macro_deck->line()-num_lines);
  }

  RDLogModel::remove(line,num_lines,preserv_custom_transition);
  if(update) {
    if(nextLine()>line) {
      makeNext(nextLine()-num_lines);
    }
    UpdateStartTimes();
    if(lineCount()==0) {
      emit reloaded();
    }
    if(mod_line>=0) {
      emit modified(mod_line);
    }
    ChangeTransport();
    SetTransTimer();
    UpdatePostPoint();
  }
}


void RDLogPlay::move(int from_line,int to_line)
{
  int offset=0;
  int lines[TRANSPORT_QUANTITY];
  RDLogLine *logline;
  RDPlayDeck *playdeck;
  int mod_line[2]={-1,-1};

  if(from_line<(lineCount()-1)) {
    if(logLine(from_line+1)->hasCustomTransition()) {
      if(from_line<to_line) {
	mod_line[0]=from_line;
      }
      else {
	mod_line[0]=from_line+1;
      }
    }
  }
  if(to_line<lineCount()) {
    if(logLine(to_line)->hasCustomTransition()) {
      if(from_line>to_line) {
	mod_line[1]=to_line;
      }
      else {
	mod_line[1]=to_line+1;
      }
    }
  }

  emit removed(from_line,1,true);
  int running=runningEvents(lines,false);
  for(int i=0;i<running;i++) {
    if((logline=logLine(lines[i]))!=NULL) {
      playdeck=(RDPlayDeck *)logline->playDeck();
      if(playdeck->id()>=0) {
	if((playdeck->id()>from_line)&&
	   (playdeck->id()<=to_line)) {
	  playdeck->setId(playdeck->id()-1);
	}
	else {
	  if((playdeck->id()<from_line)&&
	     (playdeck->id()>to_line)) {
	    playdeck->setId(playdeck->id()+1);
	  }
	}
      }
    }
  }
  if(play_macro_deck->line()>=0) {
    if((play_macro_deck->line()>from_line)&&
       (play_macro_deck->line()<=to_line)) {
      play_macro_deck->setLine(play_macro_deck->line()-1);
    }
    else {
      if((play_macro_deck->line()<from_line)&&
	 (play_macro_deck->line()>to_line)) {
	play_macro_deck->setLine(play_macro_deck->line()+1);
      }
    }
  }

  if(to_line>from_line) {
    offset=1;
  }
  RDLogModel::move(from_line,to_line);
  UpdateStartTimes();
  SetTransTimer();
  UpdatePostPoint();
  emit inserted(to_line);
  for(int i=0;i<2;i++) {
    if(mod_line[i]>=0) {
      emit modified(mod_line[i]);
    }
  }
  if((nextLine()>from_line)&&(nextLine()<=(to_line+offset))) {
    makeNext(nextLine()-1);
  }
  else {
    if((nextLine()<from_line)&&(nextLine()>to_line)) {
      makeNext(nextLine()+1);
    }
    else {
      ChangeTransport();
    }
  }
}


void RDLogPlay::copy(int from_line,int to_line,RDLogLine::TransType type)
{
  RDLogLine *logline;

  if((logline=logLine(from_line))==NULL) {
    return;
  }
  insert(to_line,logline->cartNumber(),RDLogLine::Play,type);
}


int RDLogPlay::topLine()
{
  for(int i=0;i<lineCount();i++) {
    if((logLine(i)->status()==RDLogLine::Playing)||
       (logLine(i)->status()==RDLogLine::Finishing)||
       (logLine(i)->status()==RDLogLine::Paused)) {
      return i;
    }
  }
  return nextLine();
}


int RDLogPlay::currentLine() const
{
  return play_line_counter;
}


int RDLogPlay::nextLine() const
{
  return play_next_line;
}


int RDLogPlay::nextLine(int line)
{
  int lines[TRANSPORT_QUANTITY];

  // FIXME: Do we really need this codeblock?
  transportEvents(lines);
  for(int i=0;i<(TRANSPORT_QUANTITY-1);i++) {
    if(line==lines[i]) {
      for(int j=i+1;j<TRANSPORT_QUANTITY;j++) {
	if(logLine(lines[j])==NULL) {
	  return -1;
	}
	if(logLine(lines[j])->status()==RDLogLine::Scheduled) {
	  return lines[j];
	}
      }
    }
  }
  // End of FIXME

  for(int i=line+1;i<lineCount();i++) {
    if(logLine(i)->status()==RDLogLine::Scheduled) {
      return i;
    }
  }
  return -1;
}


RDLogLine *RDLogPlay::nextEvent()
{
  if(play_next_line<0) {
    return NULL;
  }
  return logLine(play_next_line);
}


RDLogLine::TransType RDLogPlay::nextTrans()
{
  RDLogLine *logline=nextEvent();
  if(logline==NULL) {
    return RDLogLine::Stop;
  }
  return logline->transType();
}


RDLogLine::TransType RDLogPlay::nextTrans(int line)
{
  RDLogLine *logline;
  int next_line; 

  next_line=nextLine(line);
  logline=logLine(next_line);
  if(logline!=NULL) {
    return logline->transType();
  }
  return RDLogLine::Stop;
}


void RDLogPlay::transportEvents(int line[])
{
  int count=0;
  int start=topLine();
  RDLogLine *logline;

  for(int i=0;i<TRANSPORT_QUANTITY;i++) {
    line[i]=-1;
  }
  if((start<0)||(lineCount()==0)) {
    return;
  }

  count=runningEvents(line);
  if(nextLine()<0) {
    return;
  }
  start=play_next_line;
  if((logline=logLine(start))==NULL) {
    return;
  }
  for(int i=start;i<lineCount();i++) {
    if((logline=logLine(i))==NULL) {
      return;
    }
    switch(logline->status()) {
    case RDLogLine::Scheduled:
      if(count<TRANSPORT_QUANTITY) {
	line[count++]=i;
      }
      break;

    default:
      break;
    }
    if(count==TRANSPORT_QUANTITY) {
      return;
    }
  }
}


int RDLogPlay::runningEvents(int *lines, bool include_paused)
{
  int count=0;
  int events[TRANSPORT_QUANTITY];
  int table[TRANSPORT_QUANTITY];
  bool changed=true;

  if(lineCount()==0) {
    return 0;
  }
  for(int i=0;i<TRANSPORT_QUANTITY;i++) {
    if (lines){
      lines[i]=-1;
    }
    table[i]=i;
  }

  //
  // Build Running Event List
  //
  if(include_paused) {
    for(int i=0;i<lineCount();i++) {
      if((logLine(i)->status()==RDLogLine::Playing)||
	 (logLine(i)->status()==RDLogLine::Finishing)||
	 (logLine(i)->status()==RDLogLine::Paused)) {
	events[count++]=i;
	if(count==TRANSPORT_QUANTITY) {
	  break;
	}
      }
    }
  }
  else {
    for(int i=0;i<lineCount();i++) {
      if((logLine(i)->status()==RDLogLine::Playing)||
	 (logLine(i)->status()==RDLogLine::Finishing)) {
	events[count++]=i;
	if(count==TRANSPORT_QUANTITY) {
	  break;
	}
      }
    }
  }
  if (!lines){
    return count;
  }
  //
  // Sort 'Em (by start time)
  //
  while(changed) {
    changed=false;
    for(int i=0;i<(count-1);i++) {
      if(logLine(events[table[i]])->startTime(RDLogLine::Initial)>
	 logLine(events[table[i+1]])->startTime(RDLogLine::Initial)) {
	int event=table[i];
	table[i]=table[i+1];
	table[i+1]=event;
	changed=true;
      }
    }
  }

  //
  // Write out the table
  //
  for(int i=0;i<count;i++) {
    lines[i]=events[table[i]];
  }

  return count;
}


void RDLogPlay::lineModified(int line)
{
  RDLogLine *logline;
  RDLogLine *next_logline;

  SetTransTimer();
  UpdateStartTimes();

  if((logline=logLine(line))!=NULL) {
    if((next_logline=logLine(line+1))==NULL) {
      logline->loadCart(logline->cartNumber(),RDLogLine::Play,
			play_id,logline->timescalingActive());
    }
    else {
      logline->loadCart(logline->cartNumber(),next_logline->transType(),
			play_id,logline->timescalingActive());
    }
  }
  emit modified(line);
  int lines[TRANSPORT_QUANTITY] = {-1};
  int count;
  count = runningEvents(lines,false);
  if (count > 0){
    line=lines[count-1];
  }
  UpdatePostPoint();
  ChangeTransport();
}


RDLogLine::Status RDLogPlay::status(int line)
{
  RDLogLine *logline;

  if((logline=logLine(line))==NULL) {
    return RDLogLine::Scheduled;
  }
  return logline->status();
}


QTime RDLogPlay::startTime(int line)
{
  RDLogLine *logline;

  if((logline=logLine(line))==NULL) {
    return QTime();
  }
  switch(logline->cartType()) {
  case RDCart::Audio:
    if(((RDPlayDeck *)logline->playDeck())==NULL) {
      return logline->startTime(RDLogLine::Predicted);
    }
    return logline->startTime(RDLogLine::Actual);
    break;

  case RDCart::Macro:
  case RDCart::All:
    return logline->startTime(RDLogLine::Predicted);
  break;
  }
  return QTime();
}


QTime RDLogPlay::nextStop() const
{
  return play_next_stop;
}


int RDLogPlay::startOfHour(int hour) const
{
  for(int i=0;i<lineCount();i++) {
    RDLogLine *ll=logLine(i);
    if(ll->startTime(RDLogLine::Predicted).isValid()) {
      if(ll->startTime(RDLogLine::Predicted).hour()==hour) {
	return i;
      }
    }
    if(ll->startTime(RDLogLine::Imported).isValid()) {
      if(ll->startTime(RDLogLine::Imported).hour()==hour) {
	return i;
      }
    }
  }
  return -1;
}


bool RDLogPlay::running(bool include_paused)
{
  if(runningEvents(NULL,include_paused)==0) {
    return false;
  }
  return true;
}


void RDLogPlay::resync()
{
  SetTransTimer();
}


bool RDLogPlay::isRefreshable() const
{
  if(play_log==NULL) {
    return false;
  }
  return (play_log->exists())&&
    (play_log->linkDatetime()==play_link_datetime)&&
    (play_log->modifiedDatetime()>play_modified_datetime);
}


void RDLogPlay::setSlotQuantity(int slot_quan)
{
  if(slot_quan!=play_slot_quantity) {
    play_slot_quantity=slot_quan;
    QVector<int> roles;
    roles.push_back(Qt::BackgroundRole);
    emit dataChanged(createIndex(play_next_line,0),
		     createIndex(play_next_line+play_slot_quantity-1,columnCount()),roles);
  }
}


void RDLogPlay::transTimerData()
{
  int lines[TRANSPORT_QUANTITY];
  RDLogLine *logline=NULL;
  int grace=0;
  int trans_line=play_trans_line;
  int running_events=runningEvents(lines);

  if(play_grace_timer->isActive()) {
    play_grace_timer->stop();
  }

  if(play_op_mode==RDAirPlayConf::Auto) {
    if((logline=logLine(play_trans_line))!=NULL) {
      if(logline->graceTime()==-1) {  // Make Next
	makeNext(play_trans_line);
	SetTransTimer();
	return;
      }
      if(logline->graceTime()>0) {
	if(running_events>0) {
	  if(logline->transType()==RDLogLine::Stop) {
	    logline->setTransType(RDLogLine::Play);
	  }
	  logline->setStartTime(RDLogLine::Predicted,logline->
				startTime(RDLogLine::Predicted).
				addMSecs(grace));
	  play_grace_line=play_trans_line;
	  play_grace_timer->start(logline->graceTime());
	  return;
	}
	else {
	}
      }
    }
    if(!GetNextPlayable(&play_trans_line,false)) {
      SetTransTimer();
      return;
    }
    if((logline=logLine(play_trans_line))!=NULL) {
      grace=logline->graceTime();
    }
    makeNext(play_trans_line);
    if(logline->transType()!=RDLogLine::Stop || grace>=0) {
      if(play_trans_length>0) {
	StartEvent(trans_line,RDLogLine::Segue,play_trans_length,
		   RDLogLine::StartTime);
      }
      else {
	StartEvent(trans_line,RDLogLine::Play,0,RDLogLine::StartTime);
      }
    }
  }
  SetTransTimer();
}


void RDLogPlay::graceTimerData()
{
  int lines[TRANSPORT_QUANTITY];
  int line=play_grace_line;

  if(play_op_mode==RDAirPlayConf::Auto) {
    if(!GetNextPlayable(&line,false)) {
      SetTransTimer();
      return;
    }
    if((runningEvents(lines)==0)) {
      makeNext(play_grace_line);
      StartEvent(play_grace_line,RDLogLine::Play,0,RDLogLine::StartTime);
    }
    else {
      makeNext(play_grace_line);
      if(play_trans_length==0) {
	StartEvent(play_grace_line,RDLogLine::Play,0,RDLogLine::StartTime);
      }
      else {
	StartEvent(play_grace_line,RDLogLine::Segue,play_trans_length,
		   RDLogLine::StartTime);
      }
    }
  }
}


void RDLogPlay::playStateChangedData(int id,RDPlayDeck::State state)
{
#ifdef SHOW_SLOTS
  printf("playStateChangedData(%d,%d), log: %s\n",id,state,(const char *)logName());
#endif
  switch(state) {
  case RDPlayDeck::Playing:
    Playing(id);
    break;

  case RDPlayDeck::Paused:
    Paused(id);
    break;

  case RDPlayDeck::Stopping:
    Stopping(id);
    break;

  case RDPlayDeck::Stopped:
    Stopped(id);
    break;

  case RDPlayDeck::Finished:
    Finished(id);
    break;
  }
}


void RDLogPlay::onairFlagChangedData(bool state)
{
  play_onair_flag=state;
}


void RDLogPlay::segueStartData(int id)
{
#ifdef SHOW_SLOTS
  printf("segueStartData(%d)\n",id);
#endif
  int line=GetLineById(id);
  RDLogLine *logline;
  RDLogLine *next_logline=nextEvent();
  if(next_logline==NULL) {
    return;
  }
  if((logline=logLine(line))==NULL) {
    return;
  }
  if((play_op_mode==RDAirPlayConf::Auto)&&
     ((next_logline->transType()==RDLogLine::Segue))&&
     (logline->status()==RDLogLine::Playing)&&
     (logline->id()!=-1)) {
    if(!GetNextPlayable(&play_next_line,false)) {
      return;
    }
    StartEvent(play_next_line,next_logline->transType(),
	       logline->segueTail(next_logline->transType()),
	       RDLogLine::StartSegue,-1,
	       logline->segueTail(next_logline->transType()));
    SetTransTimer();
  }
}


void RDLogPlay::segueEndData(int id)
{
#ifdef SHOW_SLOTS
  printf("segueEndData(%d)\n",id);
#endif

  int line=GetLineById(id);
  RDLogLine *logline;
  if((logline=logLine(line))==NULL) {
    return;
  }
  if((play_op_mode==RDAirPlayConf::Auto)&&
     (logline->status()==RDLogLine::Finishing)) {
    ((RDPlayDeck *)logline->playDeck())->stop();
    CleanupEvent(id);
    UpdateStartTimes();
    LogTraffic(logline,(RDLogLine::PlaySource)(play_id+1),
	       RDAirPlayConf::TrafficFinish,play_onair_flag);
    emit stopped(line);
    ChangeTransport();
  }
}


void RDLogPlay::talkStartData(int id)
{
#ifdef SHOW_SLOTS
  printf("talkStartData(%d)\n",id);
#endif
}


void RDLogPlay::talkEndData(int id)
{
#ifdef SHOW_SLOTS
  printf("talkEndData(%d)\n",id);
#endif
}


void RDLogPlay::positionData(int id,int pos)
{
  int line=GetLineById(id);

  RDLogLine *logline;
  if((logline=logLine(line))==NULL) {
    return;
  }
  if(pos>logline->effectiveLength()) {
    return;
  }
  logline->setPlayPosition(pos);
  emit position(line,pos);
}


void RDLogPlay::macroStartedData()
{
#ifdef SHOW_SLOTS
  printf("macroStartedData()\n");
#endif
  play_macro_running=true;
  int line=play_macro_deck->line();
  RDLogLine *logline;
  if((logline=logLine(line))==NULL) {
    return;
  }
  logline->setStatus(RDLogLine::Playing);
  logline->
    setStartTime(RDLogLine::Initial,
		 QTime::currentTime().addMSecs(rda->station()->timeOffset()));
  UpdateStartTimes();
  emit played(line);
  UpdatePostPoint();
  ChangeTransport();
}


void RDLogPlay::macroFinishedData()
{
#ifdef SHOW_SLOTS
  printf("macroFinishedData()\n");
#endif
  int line=play_macro_deck->line();
  play_macro_deck->clear();
  FinishEvent(line);
  RDLogLine *logline;
  if((logline=logLine(line))!=NULL) {
    logline->setStatus(RDLogLine::Finished);
   LogTraffic(logline,(RDLogLine::PlaySource)(play_id+1),
	      RDAirPlayConf::TrafficMacro,play_onair_flag);
  }
  play_macro_running=false;
  UpdatePostPoint();
  if(play_refresh_pending) {
    refresh();
    play_refresh_pending=false;
  }
  ChangeTransport();
}


void RDLogPlay::macroStoppedData()
{
#ifdef SHOW_SLOTS
  printf("macroStoppedData()\n");
#endif
  int line=play_macro_deck->line();
  play_macro_deck->clear();
  RDLogLine *logline;
  if((logline=logLine(line))!=NULL) {
    logline->setStatus(RDLogLine::Finished);
   LogTraffic(logline,(RDLogLine::PlaySource)(play_id+1),
	      RDAirPlayConf::TrafficMacro,play_onair_flag);
  }
  UpdatePostPoint();
  ChangeTransport();
}


void RDLogPlay::timescalingSupportedData(int card,bool state)
{
  if(card>=0) {
    play_timescaling_supported[card]=state;
    if(play_timescaling_supported[play_card[0]]&&
       play_timescaling_supported[play_card[1]]) {
      play_timescaling_available=true;
    }
    else {
      play_timescaling_available=false;
    }
  }
  else {
    play_timescaling_available=false;
  }
}


void RDLogPlay::auditionStartedData()
{
  if(play_audition_head_played) {
    emit auditionHeadPlayed(play_audition_line);
  }
  else {
    emit auditionTailPlayed(play_audition_line);
  }
}


void RDLogPlay::auditionStoppedData()
{
  int line=play_audition_line;
  play_audition_line=-1;
  emit auditionStopped(line);
}


void RDLogPlay::notificationReceivedData(RDNotification *notify)
{
  RDLogLine *ll=NULL;
  RDLogLine *next_ll=NULL;

  if(notify->type()==RDNotification::CartType) {
    unsigned cartnum=notify->id().toUInt();
    for(int i=0;i<lineCount();i++) {
      if((ll=logLine(i))!=NULL) {
	if((ll->cartNumber()==cartnum)&&(ll->status()==RDLogLine::Scheduled)&&
	   ((ll->type()==RDLogLine::Cart)||(ll->type()==RDLogLine::Macro))) {
	  switch(ll->state()) {
	  case RDLogLine::Ok:
	  case RDLogLine::NoCart:
	  case RDLogLine::NoCut:
	    if((next_ll=logLine(i+1))!=NULL) {
	      ll->loadCart(ll->cartNumber(),next_ll->transType(),play_id,
			   ll->timescalingActive());
	    }
	    else {
	      ll->loadCart(ll->cartNumber(),RDLogLine::Play,play_id,
			   ll->timescalingActive());
	    }
	    emit modified(i);
	    break;
	    
	  default:
	    break;
	  }
	}
      }
    }
  }

  if(notify->type()==RDNotification::LogType) {
    //
    // Check Refreshability
    //
    if((play_log!=NULL)&&(notify->id().toString()==play_log->name())) {
      if((!play_log->exists())||(play_log->linkDatetime()!=play_link_datetime)||
	 (play_log->modifiedDatetime()<=play_modified_datetime)) {
	if(play_refreshable) {
	  play_refreshable=false;
	  emit refreshabilityChanged(play_refreshable);
	}
      }
      else {
	if(play_log->autoRefresh()) {
	  refresh();
	}
	else {
	  if(!play_refreshable) {
	    play_refreshable=true;
	    emit refreshabilityChanged(play_refreshable);
	  }
	}
      }
    }
  }
}


QString RDLogPlay::cellText(int col,int row,RDLogLine *ll) const
{
  if(col==0) {  // Start time
    if((ll->status()==RDLogLine::Scheduled)||
       (ll->status()==RDLogLine::Paused)) {
      if(ll->timeType()==RDLogLine::Hard) {
	if(ll->graceTime()<0) {
	  return tr("S")+
	    rda->tenthsTimeString(ll->startTime(RDLogLine::Logged));
	}
	return tr("H")+rda->tenthsTimeString(ll->startTime(RDLogLine::Logged));
      }
      if(!ll->startTime(RDLogLine::Predicted).isNull()) {
	return rda->tenthsTimeString(ll->startTime(RDLogLine::Predicted));
      }
      return QString("");
    }
    return rda->tenthsTimeString(ll->startTime(RDLogLine::Actual));
  }

  if(((ll->cutNumber()<0)&&(ll->type()==RDLogLine::Cart))) {
    if(col==5) {  // Title
      if(ll->state()==RDLogLine::NoCart) {
	return tr("[CART NOT FOUND]");
      }
    }
    if(col==6) {  // Artist
      if(ll->state()==RDLogLine::NoCut) {
	return tr("[NO AUDIO AVAILABLE]");
      }
    }
  }

  return RDLogModel::cellText(col,row,ll);
}


QFont RDLogPlay::cellTextFont(int col,int row,RDLogLine *ll) const
{
  if(ll->timeType()==RDLogLine::Hard) {
    return boldFont();
  }

  return RDLogModel::cellTextFont(col,row,ll);
}


QColor RDLogPlay::cellTextColor(int col,int row,RDLogLine *ll) const
{
  if(col==3) {
    return ll->groupColor();
  }
  if(ll->timeType()==RDLogLine::Hard) {
    return Qt::blue;
  }

  return RDLogModel::cellTextColor(col,row,ll);
}


QColor RDLogPlay::rowBackgroundColor(int row,RDLogLine *ll) const
{
  switch(ll->status()) {
  case RDLogLine::Scheduled:
  case RDLogLine::Auditioning:
    if((ll->type()==RDLogLine::Cart)&&
       (ll->state()==RDLogLine::NoCart)) {
      return LOG_ERROR_COLOR;
    }
    else {
      if(((ll->cutNumber()<0)&&(ll->type()==RDLogLine::Cart))||
	 (ll->state()==RDLogLine::NoCut)) {
	if((play_next_line>=0)&&(play_next_line==row)) {
	  return LOG_NEXT_COLOR;
	}
	return LOG_ERROR_COLOR;
      }
      else {
	if((play_next_line>=0)&&(play_slot_quantity>0)&&
	   (row>=play_next_line)&&(row<(play_next_line+play_slot_quantity-1))) {
	  if(ll->evergreen()) {
	    return LOG_EVERGREEN_COLOR;
	  }
	  else {
	    return LOG_NEXT_COLOR;
	  }
	}
	else {
	  if(ll->evergreen()) {
	    return LOG_EVERGREEN_COLOR;
	  }
	  else {
	    return LOG_SCHEDULED_COLOR;
	  }
	}
      }
    }
    break;
	
  case RDLogLine::Playing:
  case RDLogLine::Finishing:
    return LOG_PLAYING_COLOR;
	
  case RDLogLine::Paused:
    return LOG_PAUSED_COLOR;
	
  case RDLogLine::Finished:
    if(ll->state()==RDLogLine::Ok) {
      return LOG_FINISHED_COLOR;
    }
    return LOG_ERROR_COLOR;
  }

  return RDLogModel::rowBackgroundColor(row,ll);
}


bool RDLogPlay::StartEvent(int line,RDLogLine::TransType trans_type,
			   int trans_length,RDLogLine::StartSource src,
			   int mport,int duck_length)
{
  int running;
  int lines[TRANSPORT_QUANTITY];
  RDLogLine *logline;
  RDLogLine *next_logline;
  RDPlayDeck *playdeck;
  int card;
  int port;
  int aport;
  bool was_paused=false;

  if(!channelsValid()) {
    return false;
  }
  if((logline=logLine(line))==NULL) {
    return false;
  }
  if(logline->id()<0) {
    return false;
  }

  //
  // Transition running events
  //
  running=runningEvents(lines);
  if(play_op_mode!=RDAirPlayConf::Manual) {
    switch(trans_type) {
    case RDLogLine::Play:
      for(int i=0;i<running;i++) {
	if(logLine(lines[i])!=NULL) {
	  if(((logLine(lines[i])->type()==RDLogLine::Cart)||
	      (logLine(lines[i])->type()==RDLogLine::Macro))&&
	     (logLine(lines[i])->status()!=RDLogLine::Paused)) {
	    switch(logLine(lines[i])->cartType()) {
	    case RDCart::Audio:
	      ((RDPlayDeck *)logLine(lines[i])->playDeck())->stop();
	      break;
		  
	    case RDCart::Macro:
	      play_macro_deck->stop();
	      break;

	    case RDCart::All:
	      break;
	    }
	  }
	}
      }
      break;

    case RDLogLine::Segue:
      for(int i=0;i<running;i++) {
	RDLogLine *prev_logline=logLine(lines[i]);
	if(prev_logline!=NULL) {
	  if(prev_logline->status()==RDLogLine::Playing) {
	    if(((prev_logline->type()==RDLogLine::Cart)||
		(prev_logline->type()==RDLogLine::Macro))&&
	       (prev_logline->status()!=RDLogLine::Paused)) {
	      switch(logLine(lines[i])->cartType()) {
	      case RDCart::Audio:
		prev_logline->setStatus(RDLogLine::Finishing);
		((RDPlayDeck *)prev_logline->playDeck())->
		  stop(trans_length);
		break;

	      case RDCart::Macro:
		play_macro_deck->stop();
		break;

	      case RDCart::All:
		break;
	      }
	    }
	  }
	}
      }
      break;

    default:
      break;
    }
  }

  //
  // Clear Unplayed Custom Transition
  //
  if(logLine(line-1)!=NULL) {
    if(logLine(line-1)->status()==RDLogLine::Scheduled) {
      logLine(line-1)->clearTrackData(RDLogLine::TrailingTrans);
    }
  }

  //
  // Start Playout
  //
  logline->setStartSource(src);
  switch(logline->type()) {
  case RDLogLine::Cart:
    if(!StartAudioEvent(line)) {
      UpdateRestartData();
      return false;
    }
    aport=GetNextChannel(mport,&card,&port);
    playdeck=(RDPlayDeck *)logline->playDeck();
    playdeck->setCard(card);
    playdeck->setPort(port);
    playdeck->setChannel(aport);
    logline->setPauseCard(card);
    logline->setPausePort(port);
    logline->setPortName(GetPortName(playdeck->card(),
				     playdeck->port()));
    if(logline->portName().toInt()==2){
      playdeck->duckVolume(play_duck_volume_port2,0);
    }
    else  {
      playdeck->duckVolume(play_duck_volume_port1,0);
    }
		
    if(!playdeck->setCart(logline,logline->status()!=RDLogLine::Paused)) {
      // No audio to play, so fake it
      logline->setZombified(true);
      playStateChangedData(playdeck->id(),RDPlayDeck::Playing);
      logline->setStatus(RDLogLine::Playing);
      playStateChangedData(playdeck->id(),RDPlayDeck::Finished);
      logline->setStatus(RDLogLine::Finished);
      rda->syslog(LOG_WARNING,
		  "log engine: RDLogPlay::StartEvent(): no audio,CUT=%s",
		  (const char *)logline->cutName().toUtf8());
      UpdateRestartData();
      return false;
    }
    emit modified(line);
    logline->setCutNumber(playdeck->cut()->cutNumber());
    logline->setEvergreen(playdeck->cut()->evergreen());
    if(play_timescaling_available&&logline->enforceLength()) {
      logline->setTimescalingActive(true);
    }
    play_cae->setOutputVolume(playdeck->card(),playdeck->stream(),
			      playdeck->port(),playdeck->cut()->playGain());
    if((int)logline->playPosition()>logline->effectiveLength()) {
      rda->syslog(LOG_DEBUG,"log engine: *** position out of bounds: Line: %d  Cart: %d  Pos: %d ***",line,logline->cartNumber(),logline->playPosition());
      logline->setPlayPosition(0);
    }
    playdeck->play(logline->playPosition(),-1,-1,duck_length);
    if(logline->status()==RDLogLine::RDLogLine::Paused) {
      logline->
	setStartTime(RDLogLine::Actual,playdeck->startTime());
      was_paused=true;
    }
    else {
      logline->
	setStartTime(RDLogLine::Initial,playdeck->startTime());
    }
    logline->setStatus(RDLogLine::Playing);
    if(!play_start_rml[aport].isEmpty()) {
      play_event_player->
	exec(logline->resolveWildcards(play_start_rml[aport]));
    }
    emit channelStarted(play_id,playdeck->channel(),
			playdeck->card(),playdeck->port());
    rda->syslog(LOG_INFO,"log engine: started audio cart: Line: %d  Cart: %u  Cut: %u Pos: %d  Card: %d  Stream: %d  Port: %d",
		line,logline->cartNumber(),
		playdeck->cut()->cutNumber(),
		logline->playPosition(),
		playdeck->card(),
		playdeck->stream(),
		playdeck->port());

    //
    // Assign Next Event
    //
    if((play_next_line>=0)&&(!was_paused)) {
      play_next_line=line+1;
      if((next_logline=logLine(play_next_line))!=NULL) {
	if(next_logline->id()==-2) {
	  play_start_next=false;
	}
      }
      emit nextEventChanged(play_next_line);
    }
    break;

  case RDLogLine::Macro:
    //
    // Assign Next Event
    //
    if(play_next_line>=0) {
      play_next_line=line+1;
      if((next_logline=logLine(play_next_line))!=NULL) {
	if(logline->id()==-2) {
	  play_start_next=false;
	}
	if(logline->forcedStop()) {
	  next_logline->setTransType(RDLogLine::Stop);
	}
      }
    }
    if(logline->asyncronous()) {
      RDMacro *rml=new RDMacro();
      rml->setCommand(RDMacro::EX);
      QHostAddress addr;
      addr.setAddress("127.0.0.1");
      rml->setAddress(addr);
      rml->setRole(RDMacro::Cmd);
      rml->setEchoRequested(false);
      rml->addArg(logline->cartNumber());  // Arg 0
      rda->ripc()->sendRml(rml);
      delete rml;
      emit played(line);
      logline->setStartTime(RDLogLine::Actual,QTime::currentTime());
      logline->setStatus(RDLogLine::Finished);
      LogTraffic(logline,(RDLogLine::PlaySource)(play_id+1),
		 RDAirPlayConf::TrafficMacro,play_onair_flag);
      FinishEvent(line);
      ChangeTransport();
      rda->syslog(LOG_INFO,
		  "log engine: asynchronously executed macro cart: Line: %d  Cart: %u",
		  line,logline->cartNumber());
    }
    else {
      play_macro_deck->load(logline->cartNumber());
      play_macro_deck->setLine(line);
      rda->syslog(LOG_INFO,
		  "log engine: started macro cart: Line: %d  Cart: %u",
		  line,logline->cartNumber());
      play_macro_deck->exec();
    }
    break;

  case RDLogLine::Marker:
  case RDLogLine::Track:
  case RDLogLine::MusicLink:
  case RDLogLine::TrafficLink:
    //
    // Assign Next Event
    //
    if(play_next_line>=0) {
      play_next_line=line+1;
      if((next_logline=logLine(play_next_line))!=NULL) {
	if(logLine(play_next_line)->id()==-2) {
	  play_start_next=false;
	}
      }
      else {
	play_start_next=false;
      }
    }

    //
    // Skip Past
    //
    logline->setStatus(RDLogLine::Finished);
    UpdateStartTimes();
    emit played(line);
    FinishEvent(line);
    emit nextEventChanged(play_next_line);
    break;

  case RDLogLine::Chain:
    play_grace_timer->stop();

    //
    // Assign Next Event
    //
    if(play_next_line>0) {
      play_next_line=line+1;
      if((next_logline=logLine(play_next_line))!=NULL) {
	if(logLine(play_next_line)->id()==-2) {
	  play_start_next=false;
	}
      }
      else {
	play_start_next=false;
      }
    }
    if(GetTransType(logline->markerLabel(),0)!=RDLogLine::Stop) {
      play_macro_deck->
	load(QString::asprintf("LL %d %s -2!",
			       play_id+1,
			       logline->markerLabel().toUtf8().constData()));
    }
    else {
      play_macro_deck->
	load(QString::asprintf("LL %d %s -2!",
			       play_id+1,
			       logline->markerLabel().toUtf8().constData()));
    }
    play_macro_deck->setLine(line);
    play_macro_deck->exec();
    rda->syslog(LOG_INFO,"log engine: chained to log: Line: %d  Log: %s",
		line,logline->markerLabel().toUtf8().constData());
    break;

  default:
    break;
  }
  while((play_next_line<lineCount())&&((logline=logLine(play_next_line))!=
				       NULL)) {
    if((logline->state()==RDLogLine::Ok)||
       (logline->state()==RDLogLine::NoCart)||
       (logline->state()==RDLogLine::NoCut)) {
      UpdateRestartData();
      return true;
    }
    play_next_line++;
  }
  play_next_line=-1;
  UpdateRestartData();
  return true;
}


bool RDLogPlay::StartAudioEvent(int line)
{
  RDLogLine *logline;
  RDPlayDeck *playdeck=NULL;

  if((logline=logLine(line))==NULL) {
    return false;
  }

  //
  // Get a Play Deck
  //
  if(logline->status()!=RDLogLine::Paused) {
    logline->setPlayDeck(GetPlayDeck());
    if(logline->playDeck()==NULL) {
      return false;
    }
    playdeck=(RDPlayDeck *)logline->playDeck();
    playdeck->setId(line);
  }
  else {
    playdeck=(RDPlayDeck *)logline->playDeck();
  }

  //
  // Assign Mappings
  //
  connect(playdeck,SIGNAL(stateChanged(int,RDPlayDeck::State)),
	  this,SLOT(playStateChangedData(int,RDPlayDeck::State)));
  connect(playdeck,SIGNAL(position(int,int)),
	  this,SLOT(positionData(int,int)));
  connect(playdeck,SIGNAL(segueStart(int)),
	  this,SLOT(segueStartData(int)));
  connect(playdeck,SIGNAL(segueEnd(int)),
	  this,SLOT(segueEndData(int)));
  connect(playdeck,SIGNAL(talkStart(int)),
	  this,SLOT(talkStartData(int)));
  connect(playdeck,SIGNAL(talkEnd(int)),
	  this,SLOT(talkEndData(int)));

  return true;
}


void RDLogPlay::CleanupEvent(int id)
{
  int line=GetLineById(id);
  bool top_changed=false;
  RDLogLine *logline;
  RDPlayDeck *playdeck=NULL;
  if((logline=logLine(line))==NULL) {
    return;
  }
  playdeck=(RDPlayDeck *)logline->playDeck();
  if(playdeck->cut()==NULL) {
    rda->syslog(LOG_INFO,"log engine: event failed: Line: %d  Cart: %u",line,
		logline->cartNumber());
  }
  else {
    rda->syslog(LOG_INFO,"log engine: finished event: Line: %d  Cart: %u  Cut: %u Card: %d  Stream: %d  Port: %d",
		line,logline->cartNumber(),
		playdeck->cut()->cutNumber(),
		playdeck->card(),
		playdeck->stream(),playdeck->port());
  }
  RDLogLine *prev_logline;
  if((prev_logline=logLine(line-1))==NULL) {
  }
  else {
    if((line==0)||(prev_logline->status()!=RDLogLine::Playing)) {
      play_line_counter++;
      top_changed=true;
    }
  }
  logline->setStatus(RDLogLine::Finished);
  FreePlayDeck(playdeck);
  logline->setPlayDeck(NULL);
  UpdatePostPoint();
  if(top_changed) {
    emit topEventChanged(play_line_counter);
  }
}


void RDLogPlay::UpdateStartTimes()
{
  QTime time;
  QTime new_time;
  QTime end_time;
  QTime prev_time;
  QTime next_stop;
  int running=0;
  int prev_total_length=0;
  int prev_segue_length=0;
  bool stop_set=false;
  bool stop;
  RDLogLine *logline;
  RDLogLine *next_logline;
  RDLogLine::TransType next_trans;
  int lines[TRANSPORT_QUANTITY];
  bool hours[24]={false};
  int line=play_next_line;

  if((running=runningEvents(lines,false))>0) {
    line=lines[0];
  }

  for(int i=line;i<lineCount();i++) {
    if((logline=logLine(i))!=NULL) {
      if((next_logline=logLine(nextLine(i)))!=NULL) {
	next_trans=next_logline->transType();
      }
      else {
	next_trans=RDLogLine::Stop;
      }
      stop=false;
      if((logline->status()==RDLogLine::Playing)||
	 (logline->status()==RDLogLine::Finishing)){
	time=logline->startTime(RDLogLine::Actual);
      }
      else {
	time=GetStartTime(logline->startTime(RDLogLine::Logged),
			  logline->transType(),
			  logline->timeType(),
			  time,prev_total_length,prev_segue_length,
			  &stop,running);
	logline->setStartTime(RDLogLine::Predicted,time);
      }

      if(stop&&(!stop_set)) {
	next_stop=time.addMSecs(prev_total_length);
	stop_set=true;
      }

      prev_total_length=logline->effectiveLength()-
	logline->playPosition();
      prev_segue_length=
	logline->segueLength(next_trans)-logline->playPosition();
      end_time=
	time.addMSecs(logline->effectiveLength()-
		      logline->playPosition());

      if((logline->status()==RDLogLine::Scheduled)||
	 (logline->status()==RDLogLine::Paused)) {
	prev_total_length=logline->effectiveLength()-
	  logline->playPosition();
	prev_segue_length=
	  logline->segueLength(next_trans)-logline->playPosition();
	end_time=
	  time.addMSecs(logline->effectiveLength()-logline->playPosition());
      }
      else {
	prev_total_length=logline->effectiveLength();
	prev_segue_length=logline->segueLength(next_trans);
	end_time=time.addMSecs(logline->effectiveLength());
      }
    }
  }
  next_stop=GetNextStop(line);

  if(next_stop!=play_next_stop) {
    play_next_stop=next_stop;
    emit nextStopChanged(play_next_stop);
  }

  emit dataChanged(createIndex(0,0),createIndex(0,lineCount()));

  for(int i=0;i<lineCount();i++) {
    RDLogLine *ll=logLine(i);
    if(ll->startTime(RDLogLine::Predicted).isValid()) {
      hours[ll->startTime(RDLogLine::Predicted).hour()]=true;
    }
    else {
      if(ll->startTime(RDLogLine::Imported).isValid()) {
	hours[ll->startTime(RDLogLine::Imported).hour()]=true;
      }
    }
  }
  for(int i=0;i<24;i++) {
    if(hours[i]!=play_hours[i]) {
      emit hourChanged(i,hours[i]);
      play_hours[i]=hours[i];
    }
  }

  SendNowNext();
}


void RDLogPlay::FinishEvent(int line)
{
  int prev_next_line=play_next_line;
  if(GetNextPlayable(&play_next_line,false)) {
    if(play_next_line>=0) {
      RDLogLine *logline;
      if((logline=logLine(play_next_line))==NULL) {
	return;
      }
      if((play_op_mode==RDAirPlayConf::Auto)&&
	 (logline->id()!=-1)&&(play_next_line<lineCount())) {
	if(play_next_line>=0) {
	  if(logline->transType()==RDLogLine::Play) {
	    StartEvent(play_next_line,RDLogLine::Play,0,RDLogLine::StartPlay);
  	    SetTransTimer(QTime(),prev_next_line==play_trans_line);
	  }
	  if(logline->transType()==RDLogLine::Segue) {
	    StartEvent(play_next_line,RDLogLine::Segue,0,RDLogLine::StartPlay);
  	    SetTransTimer(QTime(),prev_next_line==play_trans_line);
	  }
	}
      }
    }
  }
  UpdateStartTimes();
  emit stopped(line);
}


QTime RDLogPlay::GetStartTime(QTime sched_time,
			    RDLogLine::TransType trans_type,
			    RDLogLine::TimeType time_type,QTime prev_time,
			    int prev_total_length,int prev_segue_length,
			    bool *stop,int running_events)
{
  QTime time;

  if((play_op_mode==RDAirPlayConf::LiveAssist)||
     (play_op_mode==RDAirPlayConf::Manual)) {
    *stop=true;
    return QTime();
  }
  switch(trans_type) {
  case RDLogLine::Play:
    if(!prev_time.isNull()) {
      time=prev_time.addMSecs(prev_total_length);
    }
    break;
	
  case RDLogLine::Segue:
    if(!prev_time.isNull()) {
      time=prev_time.addMSecs(prev_segue_length);
    }
    break;

  case RDLogLine::Stop:
    time=QTime();
    break;

  default:
    break;
  }
  switch(time_type) {
  case RDLogLine::Relative:
    if(!prev_time.isNull()) {
      *stop=false;
      return time;
    }
    *stop=true;
    return QTime();
    break;

  case RDLogLine::Hard:
    if((time<sched_time)||(time.isNull())) {
      *stop=true;
    }
    else {
      *stop=false;
    }
    if(running_events&&(time<sched_time)&&(trans_type!=RDLogLine::Stop)) {
      return time;
    }
    return sched_time;
    break;

  case RDLogLine::NoTime:
    break;
  }
  return QTime();
}


QTime RDLogPlay::GetNextStop(int line)
{
  bool running=false;
  QTime time;
  RDLogLine *logline;
  if((logline=logLine(line))==NULL) {
    return QTime();
  }

  for(int i=line;i<lineCount();i++) {
    if((status(i)==RDLogLine::Playing)||
       (status(i)==RDLogLine::Finishing)) {
      if((logLine(i)->type()==RDLogLine::Cart)&&
	((logLine(i)->status()==RDLogLine::Playing)||
	 (logLine(i)->status()==RDLogLine::Finishing))) {
	time=
	  startTime(i).addMSecs(logLine(i)->segueLength(nextTrans(i))-
				((RDPlayDeck *)logLine(i)->playDeck())->
				lastStartPosition());
      }
      else {
	time=startTime(i).addMSecs(logLine(i)->segueLength(nextTrans(i)));
      }
      running=true;
    }
    else {
      if(running&&(play_op_mode==RDAirPlayConf::Auto)&&
	 (status(i)==RDLogLine::Scheduled)) {
	switch(logLine(i)->transType()) {
	case RDLogLine::Stop:
	  return time;
	  break;

	case RDLogLine::Play:
	case RDLogLine::Segue:
	  time=time.addMSecs(logLine(i)->segueLength(nextTrans(i))-
			     logLine(i)->playPosition());
	  break;

	default:
	  break;
	}
      }
    }
  }
  if(running!=play_running) {
    play_running=running;
    emit runStatusChanged(running);
  }
  return time;
}

void RDLogPlay::UpdatePostPoint()
{
  int lines[TRANSPORT_QUANTITY] = {-1};
  int count = runningEvents(lines,false);
  if (count > 0){
    UpdatePostPoint(lines[count -1]);
    return;
  }
  transportEvents(lines);
  UpdatePostPoint(lines[0]);
}

void RDLogPlay::UpdatePostPoint(int line)
{
  int post_line=-1;
  QTime post_time;
  int offset=0;

  if((line<0)||(play_trans_line<0)) {
    post_line=-1;
    post_time=QTime();
    offset=0;
  }
  else {
    if((line<lineCount())&&(play_trans_line>=0)&&
       (play_trans_line<lineCount())) {
      post_line=play_trans_line;
      post_time=logLine(post_line)->startTime(RDLogLine::Logged);
      offset=length(line,post_line)-QTime::currentTime().msecsTo(post_time)-
	logLine(line)->playPosition();
    }
  }
  if((post_time!=play_post_time)||(offset!=play_post_offset)) {
    play_post_time=post_time;
    play_post_offset=offset;
    emit postPointChanged(play_post_time,offset,post_line>=line,running(false));
  }
}


void RDLogPlay::AdvanceActiveEvent()
{
  int line=-1;
  RDLogLine::TransType trans=RDLogLine::Play;

  for(int i=0;i<LOGPLAY_MAX_PLAYS;i++) {
    RDLogLine *logline;
    if((logline=logLine(play_line_counter+1))!=NULL) {
      if(logline->deck()!=-1) {
	line=play_line_counter+i;
      }
    }
  }
  if(line==-1) {
    if(line!=play_active_line) {
      play_active_line=line;
      emit activeEventChanged(line,RDLogLine::Stop);
    }
  }
  else {
    if(line<(lineCount())) {
      RDLogLine *logline;
      if((logline=logLine(line+1))!=NULL) {
	trans=logLine(line+1)->transType();
      }
    }
    else {
      trans=RDLogLine::Stop;
    }
    if((line!=play_active_line)||(trans!=play_active_trans)) {
      play_active_line=line;
      play_active_trans=trans;
      emit activeEventChanged(line,trans);
    }
  }
}


QString RDLogPlay::GetPortName(int card,int port)
{
  for(int i=0;i<2;i++) {
    for(int j=0;j<2;j++) {
      if((play_card[i]==card)&&(play_port[i]==port)) {
	return play_label[i];
      }
    }
  }

  return QString();
}


void RDLogPlay::SetTransTimer(QTime current_time,bool stop)
{
  int next_line=-1;
  QTime next_time=QTime(23,59,59);

  if(current_time.isNull()) {
    current_time=QTime::currentTime();
  }
  RDLogLine *logline;

  if(play_trans_timer->isActive()) {
    if(stop) {
      play_trans_timer->stop();
    }
    else {
      return;
    }
  }
  play_trans_line=-1;
  for(int i=0;i<lineCount();i++) {
    if((logline=logLine(i))!=NULL) {
      if((logline->timeType()==RDLogLine::Hard)&&
	 ((logline->status()==RDLogLine::Scheduled)||
	  (logline->status()==RDLogLine::Auditioning))&&
	 (logline->startTime(RDLogLine::Logged)>current_time)&&
	 (logline->startTime(RDLogLine::Logged)<=next_time)) {
	next_time=logline->startTime(RDLogLine::Logged);
	next_line=i;
      }
    }
  }
  if(next_line>=0) {
    play_trans_line=next_line;
    play_trans_timer->start(current_time.msecsTo(next_time));
  }
}


int RDLogPlay::GetNextChannel(int mport,int *card,int *port)
{
  int chan=next_channel;
  if(mport<0) {
    *card=play_card[next_channel];
    *port=play_port[next_channel];
    if(++next_channel>1) {
      next_channel=0;
    }
  }
  else {
    chan=mport;
    *card=play_card[mport];
    *port=play_port[mport];
    next_channel=mport+1;
    if(next_channel>1) {
      next_channel=0;
    }
  }
  return chan;
}


int RDLogPlay::GetLineById(int id)
{
  return id;
}


RDPlayDeck *RDLogPlay::GetPlayDeck()
{
  for(int i=0;i<RD_MAX_STREAMS;i++) {
    if(!play_deck_active[i]) {
      play_deck_active[i]=true;
      return play_deck[i];
    }
  }
  return NULL;
}


void RDLogPlay::FreePlayDeck(RDPlayDeck *deck)
{
  for(int i=0;i<RD_MAX_STREAMS;i++) {
    if(play_deck[i]==deck) {
      ClearChannel(i);
      play_deck[i]->disconnect();
      play_deck[i]->reset();
      play_deck_active[i]=false;
      return;
    }
  }
}


bool RDLogPlay::GetNextPlayable(int *line,bool skip_meta,bool forced_start)
{
  RDLogLine *logline;
  RDLogLine *next_logline;
  RDLogLine::TransType next_type=RDLogLine::Play;
  int skipped=0;

  for(int i=*line;i<lineCount();i++) {
    if((logline=logLine(i))==NULL) {
      return false;
    }
    if(skip_meta&&((logline->type()==RDLogLine::Marker)||
		   (logline->type()==RDLogLine::OpenBracket)||
		   (logline->type()==RDLogLine::CloseBracket)||
		   (logline->type()==RDLogLine::Track)||
		   (logline->type()==RDLogLine::MusicLink)||
		   (logline->type()==RDLogLine::TrafficLink))) {
      logline->setStatus(RDLogLine::Finished);
      skipped++;
      emit modified(i);
    }
    else {
      if((logline->status()==RDLogLine::Scheduled)||
	 (logline->status()==RDLogLine::Paused)|| 
	 (logline->status()==RDLogLine::Auditioning)) {
        if(((logline->transType()==RDLogLine::Stop)||
	    (play_op_mode==RDAirPlayConf::LiveAssist))&&((i-skipped)!=*line)) {
	  makeNext(i);
	  return false;
        }
        if((next_logline=logLine(i+1))!=NULL) {
	  next_type=next_logline->transType();
        }
        if((logline->setEvent(play_id,next_type,logline->timescalingActive())==
	    RDLogLine::Ok)&&((logline->status()==RDLogLine::Scheduled)||
	  		     (logline->status()==RDLogLine::Paused))&&
	   (!logline->zombified())) {
	  emit modified(i);
	  *line=i;
	  return true;
        }
        else {
	  logline->setStartTime(RDLogLine::Initial,QTime::currentTime());
	  if((logline->transType()==RDLogLine::Stop)) {
            if((logline->cutNumber()>=0)&&(!logline->zombified())) {
                emit modified(i);
	        *line=i;
	        return true;
	    }
	    else {
	      if(!forced_start) {
                emit modified(i);
	        *line=i;
	        return true;
	      }
  	    }
	  }
        }
        emit modified(i);
      }
    }
  }
  return false;
}


void RDLogPlay::LogPlayEvent(RDLogLine *logline)
{
  RDCut *cut=new RDCut(QString::asprintf("%06u_%03d",
					 logline->cartNumber(),
					 logline->cutNumber()));
  cut->logPlayout();
  delete cut;
}


void RDLogPlay::RefreshEvents(int line,int line_quan,bool force_update)
{
  //
  // Check Event Status
  //
  RDLogLine *logline;
  RDLogLine *next_logline;
  RDLogLine::State state=RDLogLine::Ok;

  for(int i=line;i<(line+line_quan);i++) {
    if((logline=logLine(i))!=NULL) {
      if(logline->type()==RDLogLine::Cart) {
	switch(logline->state()) {
	case RDLogLine::Ok:
	case RDLogLine::NoCart:
	case RDLogLine::NoCut:
	  if(logline->status()==RDLogLine::Scheduled) {
	    state=logline->state();
	    if((next_logline=logLine(i+1))!=NULL) {
	      logline->
		loadCart(logline->cartNumber(),next_logline->transType(),
			 play_id,logline->timescalingActive());
	    }
	    else {
	      logline->loadCart(logline->cartNumber(),RDLogLine::Play,
				play_id,logline->timescalingActive());
	    }
	    if(force_update||(state!=logline->state())) {
	      emit modified(i);
	    }
	  }
	  break;
	  
	default:
	  break;
	}
      }
    }
  }
}


void RDLogPlay::ChangeTransport()
{
  emit transportChanged();
  if(play_next_line>=0) {
    emit dataChanged(createIndex(play_next_line,0),
		     createIndex(play_next_line+play_slot_quantity-1,
				 columnCount()));
  }
  UpdateRestartData();
}


void RDLogPlay::Playing(int id)
{
  RDLogLine *logline;

  int line=GetLineById(id);
  if((logline=logLine(line))==NULL) {
    return;
  }
  UpdateStartTimes();
  emit played(line);
  AdvanceActiveEvent();
  UpdatePostPoint();
  if (isRefreshable()&&play_log->autoRefresh()) {
    refresh();
  }
  if((logline->timeType()==RDLogLine::Hard)&&(play_grace_timer->isActive())) {
    play_grace_timer->stop();
  }
  LogPlayEvent(logline);
  ChangeTransport();
}


void RDLogPlay::Paused(int id)
{
  int line=GetLineById(id);
  RDLogLine *logline=logLine(line);
  if(logline!=NULL) {
    logline->playDeck()->disconnect();
    logline->setPortName("");
    logline->setStatus(RDLogLine::Paused);
  }
  UpdateStartTimes();
  emit paused(line);
  UpdatePostPoint();
 LogTraffic(logLine(line),(RDLogLine::PlaySource)(play_id+1),
	    RDAirPlayConf::TrafficPause,play_onair_flag);
  ChangeTransport();
}


void RDLogPlay::Stopping(int id)
{
}


void RDLogPlay::Stopped(int id)
{
  int line=GetLineById(id);
  int lines[TRANSPORT_QUANTITY];
  CleanupEvent(id);
  UpdateStartTimes();
  emit stopped(line);
  LogTraffic(logLine(line),(RDLogLine::PlaySource)(play_id+1),
	     RDAirPlayConf::TrafficStop,play_onair_flag);
  if(play_grace_timer->isActive()) {  // Pending Hard Time Event
    play_grace_timer->stop();
    play_grace_timer->start(0);
    return;
  }
  AdvanceActiveEvent();
  UpdatePostPoint();
  if(runningEvents(lines)==0) {
    next_channel=0;
  }
  ChangeTransport();
}


void RDLogPlay::Finished(int id)
{
  int line=GetLineById(id);
  RDLogLine *logline;
  int lines[TRANSPORT_QUANTITY];
  if((logline=logLine(line))==NULL) {
    return;
  }
  switch(logline->status()) {
  case RDLogLine::Playing:
    CleanupEvent(id);
    FinishEvent(line);
    break;

  case RDLogLine::Auditioning:
    break;

  default:
    break;
  }
  UpdatePostPoint();
  if(runningEvents(lines)==0) {
    next_channel=0;
  }
 LogTraffic(logline,(RDLogLine::PlaySource)(play_id+1),
	    RDAirPlayConf::TrafficFinish,play_onair_flag);
  ChangeTransport();
}


void RDLogPlay::ClearChannel(int deckid)
{
  if(play_deck[deckid]->channel()<0) {
    return;
  }
  if(play_cae->playPortActive(play_deck[deckid]->card(),
			      play_deck[deckid]->port(),
			      play_deck[deckid]->stream())) {
    return;
  }

  if(play_deck[deckid]->channel()>=0) {
    play_event_player->exec(play_stop_rml[play_deck[deckid]->channel()]);
    /*
    printf("Deck: %d  channelStopped(%d,%d,%d,%d\n",deckid,
	   play_id,play_deck[deckid]->channel(),
	   play_deck[deckid]->card(),
	   play_deck[deckid]->port());
    */
    emit channelStopped(play_id,play_deck[deckid]->channel(),
			play_deck[deckid]->card(),
			play_deck[deckid]->port());
  }
  play_deck[deckid]->setChannel(-1);
}


RDLogLine::TransType RDLogPlay::GetTransType(const QString &logname,int line)
{
  RDLogLine::TransType trans=RDLogLine::Stop;
  QString sql=QString("select `TRANS_TYPE` from `LOG_LINES` where ")+
    "`LOG_NAME`='"+RDEscapeString(logname)+"' && "+
    QString::asprintf("COUNT=%d",line);
  RDSqlQuery *q=new RDSqlQuery(sql);
  if(q->first()) {
    trans=(RDLogLine::TransType)q->value(0).toUInt();
  }
  delete q;
  return trans;
}


bool RDLogPlay::ClearBlock(int start_line)
{
  RDLogLine::Status status;

  for(int i=start_line;i<lineCount();i++) {
    status=logLine(i)->status();
    if((status!=RDLogLine::Scheduled)&&(status!=RDLogLine::Finished)) {
      remove(start_line,i-start_line);
      return true;
    }
  }
  remove(start_line,lineCount()-start_line);
  return false;
}


void RDLogPlay::SendNowNext()
{
  QTime end_time;
  QTime time;
  int now_line=-1;
  RDLogLine *logline[2];
  RDLogLine *ll;
  RDLogLine *default_now_logline=NULL;
  RDLogLine *default_next_logline=NULL;

  //
  // Get NOW PLAYING Event
  //
  int lines[TRANSPORT_QUANTITY];
  int running=runningEvents(lines,false);
  //
  // "Longest running" algorithm
  //
  /*
  for(int i=0;i<running;i++) {
    if((time=logLine(lines[i])->startTime(RDLogLine::Actual).
	addMSecs(logLine(lines[i])->effectiveLength()))>end_time) {
      end_time=time;
      now_line=lines[i];
    }
  }
  */
  /*
  //
  // "Most recently started" algorithm
  //
  if(running>0) {
    now_line=lines[running-1];  // Most recently started event
  }
  */
  //
  // "Hybrid" algorithm
  //
  if(running>0) {
    now_line=lines[running-1];  // Most recently started event
    if(logLine(now_line)->cartType()!=RDCart::Macro) {
      //
      // If the most recently started event is not a Now&Next-enabled macro
      // cart, then use longest running event instead
      //
      for(int i=0;i<running;i++) {
	if((time=logLine(lines[i])->startTime(RDLogLine::Actual).
	    addMSecs(logLine(lines[i])->effectiveLength()))>end_time) {
	  end_time=time;
	  now_line=lines[i];
	}
      }
    }
  }

  if(now_line>=0) {
    logline[0]=logLine(now_line);
  }
  else {
    if(play_now_cartnum==0) {
      logline[0]=NULL;
    }
    else {
      default_now_logline=new RDLogLine(play_now_cartnum);
      logline[0]=default_now_logline;
    }
  }

  //
  // Get NEXT Event
  //
  logline[1]=NULL;
  for(int i=nextLine();i<lineCount();i++) {
    if((ll=logLine(i))!=NULL) {
      if((ll->status()==RDLogLine::Scheduled)&&(!logLine(i)->asyncronous())) {
	logline[1]=logLine(i);
	i=lineCount();
      }
    }
  }
  if((logline[1]==NULL)&&(play_next_cartnum!=0)) {
    default_next_logline=new RDLogLine(play_next_cartnum);
    logline[1]=default_next_logline;
  }

  //
  // Process and Send It
  //
  unsigned nowcart=0;
  unsigned nextcart=0;
  if(logline[0]!=NULL) {
    if(!logline[0]->asyncronous()) {
      nowcart=logline[0]->cartNumber();
    }
  }
  if(logline[1]!=NULL) {
    nextcart=logline[1]->cartNumber();
  }
  if((nowcart==play_prevnow_cartnum)&&(nextcart==play_prevnext_cartnum)) {
    return;
  }
  if(logline[0]==NULL) {
    play_prevnow_cartnum=0;
  }
  else {
    play_prevnow_cartnum=logline[0]->cartNumber();
  }
  if(logline[1]==NULL) {
    play_prevnext_cartnum=0;
  }
  else {
    play_prevnext_cartnum=logline[1]->cartNumber();
  }
  QString svcname=play_svc_name;
  if(svcname.isEmpty()) {
    svcname=play_defaultsvc_name;
  }

  //
  // Send to PAD Server
  //
  play_pad_socket->write(QString("{\r\n").toUtf8());
  play_pad_socket->write(QString("    \"padUpdate\": {\r\n").toUtf8());
  play_pad_socket->
    write(RDJsonField("dateTime",QDateTime::currentDateTime(),8).toUtf8());
  play_pad_socket->
    write(RDJsonField("hostName",rda->station()->name(),8).toUtf8());
  play_pad_socket->
    write(RDJsonField("shortHostName",rda->station()->shortName(),8).toUtf8());
  play_pad_socket->write(RDJsonField("machine",play_id+1,8).toUtf8());
  play_pad_socket->write(RDJsonField("onairFlag",play_onair_flag,8).toUtf8());
  play_pad_socket->
    write(RDJsonField("mode",RDAirPlayConf::logModeText(play_op_mode),8).toUtf8());

  //
  // Service
  //
  if(svcname.isEmpty()) {
    play_pad_socket->write(RDJsonNullField("service",8).toUtf8());
  }
  else {
    RDSvc *svc=new RDSvc(svcname,rda->station(),rda->config(),this);
    play_pad_socket->write(QString("        \"service\": {\r\n").toUtf8());
    play_pad_socket->write(RDJsonField("name",svcname,12).toUtf8());
    play_pad_socket->
      write(RDJsonField("description",svc->description(),12).toUtf8());
    play_pad_socket->
      write(RDJsonField("programCode",svc->programCode(),12,true).toUtf8());
    play_pad_socket->write(QString("        },\r\n").toUtf8());
    delete svc;
  }

  //
  // Log
  //
  play_pad_socket->write(QString("        \"log\": {\r\n").toUtf8());
  play_pad_socket->write(RDJsonField("name",logName(),12,true).toUtf8());
  play_pad_socket->write(QString("        },\r\n").toUtf8());

  //
  // Now
  //
  QDateTime start_datetime;
  if(logline[0]!=NULL) {
    start_datetime=
      QDateTime(QDate::currentDate(),logline[0]->startTime(RDLogLine::Actual));
  }
  play_pad_socket->
    write(GetPadJson("now",logline[0],start_datetime,now_line,8,false).
	  toUtf8());

  //
  // Next
  //
  QDateTime next_datetime;
  if((mode()==RDAirPlayConf::Auto)&&(logline[0]!=NULL)) {
    next_datetime=start_datetime.addSecs(logline[0]->forcedLength()/1000);
  }
 play_pad_socket->write(GetPadJson("next",logline[1],
				   next_datetime,nextLine(),8,true).toUtf8());

  //
  // Commit the update
  //
  play_pad_socket->write(QString("    }\r\n").toUtf8());
  play_pad_socket->write(QString("}\r\n\r\n").toUtf8());

  //
  // Clean up
  //
  if(default_now_logline!=NULL) {
    delete default_now_logline;
  }
  if(default_next_logline!=NULL) {
    delete default_next_logline;
  }
}


void RDLogPlay::UpdateRestartData()
{
  QString running;
  int line=-1;
  int id=-1;
  int lines[TRANSPORT_QUANTITY];
  if(runningEvents(lines,false)>0) {
    line=lines[0];
    id=logLine(line)->id();
    running="Y";
  }
  else {
    line=nextLine();
    if((line>=0)&&(logLine(line)!=NULL)) {
      id=logLine(line)->id();
    }
    running="N";
  }

  if(line<0) {
    line=play_next_line;
    running="N";
  }
  QString sql=QString("update `LOG_MACHINES` set ")+
    QString::asprintf("`LOG_LINE`=%d,",line)+
    QString::asprintf("`LOG_ID`=%d,",id)+
    "`RUNNING`='"+running+"' "+
    "where `STATION_NAME`='"+RDEscapeString(rda->station()->name())+"' && "+
    QString::asprintf("`MACHINE`=%d",play_id);
  RDSqlQuery::apply(sql);
}


QString RDLogPlay::GetPadJson(const QString &name,RDLogLine *ll,
			      const QDateTime &start_datetime,int line,
			      int padding,bool final) const
{
  QString ret;

  if(ll==NULL) {
    ret=RDJsonNullField(name,padding,final);
  }
  else {
    ret+=RDJsonPadding(padding)+"\""+name+"\": {\r\n";
    if(start_datetime.isValid()) {
      ret+=RDJsonField("startDateTime",start_datetime,4+padding);
    }
    else {
      ret+=RDJsonNullField("startDateTime",4+padding);
    }
    ret+=RDJsonField("lineNumber",line,4+padding);
    ret+=RDJsonField("lineId",ll->id(),4+padding);
    ret+=RDJsonField("cartNumber",ll->cartNumber(),4+padding);
    ret+=RDJsonField("cartType",RDCart::typeText(ll->cartType()),4+padding);
    if(ll->cartType()==RDCart::Audio) {
      ret+=RDJsonField("cutNumber",ll->cutNumber(),4+padding);
    }
    else {
      ret+=RDJsonNullField("cutNumber",4+padding);
    }
    if(ll->useEventLength()) {
      ret+=RDJsonField("length",ll->eventLength(),4+padding);
    }
    else {
      ret+=RDJsonField("length",ll->forcedLength(),4+padding);
    }
    if(ll->year().isValid()) {
      ret+=RDJsonField("year",ll->year().year(),4+padding);
    }
    else {
      ret+=RDJsonNullField("year",4+padding);
    }
    ret+=RDJsonField("groupName",ll->groupName(),4+padding);
    ret+=RDJsonField("title",ll->title(),4+padding);
    ret+=RDJsonField("artist",ll->artist(),4+padding);
    ret+=RDJsonField("publisher",ll->publisher(),4+padding);
    ret+=RDJsonField("composer",ll->composer(),4+padding);
    ret+=RDJsonField("album",ll->album(),4+padding);
    ret+=RDJsonField("label",ll->label(),4+padding);
    ret+=RDJsonField("client",ll->client(),4+padding);
    ret+=RDJsonField("agency",ll->agency(),4+padding);
    ret+=RDJsonField("conductor",ll->conductor(),4+padding);
    ret+=RDJsonField("userDefined",ll->userDefined(),4+padding);
    ret+=RDJsonField("songId",ll->songId(),4+padding);
    ret+=RDJsonField("outcue",ll->outcue(),4+padding);
    ret+=RDJsonField("description",ll->description(),4+padding);
    ret+=RDJsonField("isrc",ll->isrc(),4+padding);
    ret+=RDJsonField("isci",ll->isci(),4+padding);
    ret+=RDJsonField("recordingMbId",ll->recordingMbId(),4+padding);
    ret+=RDJsonField("releaseMbId",ll->releaseMbId(),4+padding);
    ret+=RDJsonField("externalEventId",ll->extEventId(),4+padding);
    ret+=RDJsonField("externalData",ll->extData(),4+padding);
    ret+=RDJsonField("externalAnncType",ll->extAnncType(),4+padding,true);
    if(final) {
      ret+=RDJsonPadding(padding)+"}\r\n";
    }
    else {
      ret+=RDJsonPadding(padding)+"},\r\n";
    }
  }

  return ret;
}


void RDLogPlay::LogTraffic(RDLogLine *logline,RDLogLine::PlaySource src,
			   RDAirPlayConf::TrafficAction action,bool onair_flag)
  const
{
  QString sql;
  QDateTime datetime=QDateTime(QDate::currentDate(),QTime::currentTime());
  int length=logline->startTime(RDLogLine::Actual).msecsTo(datetime.time());
  if(length<0) {  // Event crossed midnight!
    length+=86400000;
    datetime.setDate(datetime.date().addDays(-1));
  }

  if((logline==NULL)||(serviceName().isEmpty())) {
    return;
  }

  QString evt_sql="NULL";

  if(datetime.isValid()&&logline->startTime(RDLogLine::Actual).isValid()) {
    evt_sql=RDCheckDateTime(QDateTime(datetime.date(),
	     logline->startTime(RDLogLine::Actual)), "yyyy-MM-dd hh:mm:ss");
  }
  sql=QString("insert into `ELR_LINES` set ")+
    "`SERVICE_NAME`='"+RDEscapeString(serviceName())+"',"+
    QString::asprintf("`LENGTH`=%d,",length)+
    "`LOG_NAME`='"+RDEscapeString(logName())+"',"+
    QString::asprintf("`LOG_ID`=%d,",logline->id())+
    QString::asprintf("`CART_NUMBER`=%u,",logline->cartNumber())+
    "`STATION_NAME`='"+RDEscapeString(rda->station()->name())+"',"+
    "`EVENT_DATETIME`="+evt_sql+","+
    QString::asprintf("`EVENT_TYPE`=%d,",action)+
    QString::asprintf("`EVENT_SOURCE`=%d,",logline->source())+
    "`EXT_START_TIME`="+RDCheckDateTime(logline->extStartTime(),"hh:mm:ss")+","+
    QString::asprintf("`EXT_LENGTH`=%d,",logline->extLength())+
    "`EXT_DATA`='"+RDEscapeString(logline->extData())+"',"+
    "`EXT_EVENT_ID`='"+RDEscapeString(logline->extEventId())+"',"+
    "`EXT_ANNC_TYPE`='"+RDEscapeString(logline->extAnncType())+"',"+
    QString::asprintf("`PLAY_SOURCE`=%d,",src)+
    QString::asprintf("`CUT_NUMBER`=%d,",logline->cutNumber())+
    "`EXT_CART_NAME`='"+RDEscapeString(logline->extCartName())+"',"+
    "`TITLE`='"+RDEscapeString(logline->title())+"',"+
    "`ARTIST`='"+RDEscapeString(logline->artist())+"',"+
    "`SCHEDULED_TIME`="+RDCheckDateTime(logline->startTime(RDLogLine::Logged),
				       "hh:mm:ss")+","+
    "`ISRC`='"+RDEscapeString(logline->isrc())+"',"+
    "`PUBLISHER`='"+RDEscapeString(logline->publisher())+"',"+
    "`COMPOSER`='"+RDEscapeString(logline->composer())+"',"+
    QString::asprintf("`USAGE_CODE`=%d,",logline->usageCode())+
    QString::asprintf("`START_SOURCE`=%d,",logline->startSource())+
    "`ONAIR_FLAG`='"+RDYesNo(onair_flag)+"',"+
    "`ALBUM`='"+RDEscapeString(logline->album())+"',"+
    "`LABEL`='"+RDEscapeString(logline->label())+"',"+
    "`USER_DEFINED`='"+RDEscapeString(logline->userDefined())+"',"+
    "`CONDUCTOR`='"+RDEscapeString(logline->conductor())+"',"+
    "`SONG_ID`='"+RDEscapeString(logline->songId())+"',"+
    "`DESCRIPTION`='"+RDEscapeString(logline->description())+"',"+
    "`OUTCUE`='"+RDEscapeString(logline->outcue())+"',"+
    "`ISCI`='"+RDEscapeString(logline->isci())+"'";
  RDSqlQuery::apply(sql);
}


void RDLogPlay::DumpToSyslog(int prio_lvl,const QString &hdr) const
{
  QString str;

  for(int i=0;i<lineCount();i++) {
    RDLogLine *ll=logLine(i);
    str+=QString::asprintf("count: %d: ",i);
    str+="type: "+RDLogLine::typeText(ll->type())+" ";
    switch(ll->type()) {
    case RDLogLine::Cart:
    case RDLogLine::Macro:
      str+=QString::asprintf("cartnum: %06u ",ll->cartNumber());
      str+="title: "+ll->title()+" ";
      break;

    case RDLogLine::Marker:
    case RDLogLine::Track:
    case RDLogLine::Chain:
      str+="comment: "+ll->markerComment()+" ";
      break;

    case RDLogLine::MusicLink:
    case RDLogLine::TrafficLink:
      str+="event: "+ll->linkEventName()+" ";
      str+="start time: "+ll->linkStartTime().toString("hh:mm:ss")+" ";
      str+="length: "+RDGetTimeLength(ll->linkLength(),false,false)+" ";
      break;

    case RDLogLine::OpenBracket:
    case RDLogLine::CloseBracket:
    case RDLogLine::UnknownType:
      break;
    }
    str+="\n";
  }
  rda->syslog(prio_lvl,"%s\n%s",hdr.toUtf8().constData(),
	      str.toUtf8().constData());
}
