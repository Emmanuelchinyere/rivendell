// local_macros.cpp
//
// Local RML Macros for the Rivendell Interprocess Communication Daemon
//
//   (C) Copyright 2002-2021 Fred Gleason <fredg@paravelsystems.com>
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

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <rd.h>
#include <rdapplication.h>
#include <rdconf.h>
#include <rdescape_string.h>
#include <rdmatrix.h>
#include <rdpaths.h>
#include <rdrange.h>
#include <rdtty.h>

#include "ripcd.h"

void MainObject::gpiChangedData(int matrix,int line,bool state)
{
  if(state) {
    rda->syslog(LOG_INFO,"GPI %d:%d ON",matrix,line+1);
  }
  else {
    rda->syslog(LOG_INFO,"GPI %d:%d OFF",matrix,line+1);
  }
  ripcd_gpi_state[matrix][line]=state;
  BroadcastCommand(QString::asprintf("GI %d %d %d %d!",matrix,line,state,
				     ripcd_gpi_mask[matrix][line]));
  if(!ripcd_gpi_mask[matrix][line]) {
    return;
  }
  if(ripcd_gpi_macro[matrix][line][state]>0) {
    ExecCart(ripcd_gpi_macro[matrix][line][state]);
  }
  LogGpioEvent(matrix,line,RDMatrix::GpioInput,state);
}


void MainObject::gpoChangedData(int matrix,int line,bool state)
{
  if(state) {
    rda->syslog(LOG_INFO,"GPO %d:%d ON",matrix,line+1);
  }
  else {
    rda->syslog(LOG_INFO,"GPO %d:%d OFF",matrix,line+1);
  }
  ripcd_gpo_state[matrix][line]=state;
  BroadcastCommand(QString::asprintf("GO %d %d %d %d!",matrix,line,state,
				     ripcd_gpo_mask[matrix][line]));
  if(!ripcd_gpo_mask[matrix][line]) {
    return;
  }
  if(ripcd_gpo_macro[matrix][line][state]>0) {
    ExecCart(ripcd_gpo_macro[matrix][line][state]);
  }
  LogGpioEvent(matrix,line,RDMatrix::GpioOutput,state);
}


void MainObject::gpiStateData(int matrix,unsigned line,bool state)
{
  // LogLine(RDConfig::LogWarning,QString::asprintf("gpiStateData(%d,%d,%d)",matrix,line,state));

  BroadcastCommand(QString::asprintf("GI %d %u %d %d!",matrix,line,state,
				     ripcd_gpi_mask[matrix][line]));
}


void MainObject::gpoStateData(int matrix,unsigned line,bool state)
{
  // LogLine(RDConfig::LogWarning,QString::asprintf("gpoStateData(%d,%d,%d)",matrix,line,state));

  BroadcastCommand(QString::asprintf("GO %d %u %d %d!",matrix,line,state,
				     ripcd_gpo_mask[matrix][line]));
}


void MainObject::ttyTrapData(int cartnum)
{
  rda->syslog(LOG_DEBUG,"executing trap cart %06d",cartnum);
  ExecCart(cartnum);
}



void MainObject::ttyReadyReadData(int num)
{
  char buf[256];
  int n;

  if(ripcd_tty_dev[num]!=NULL) {
    while((n=ripcd_tty_dev[num]->read(buf,255))>0) {
      buf[n]=0;
      if(ripcd_tty_trap[num]!=NULL) {
	ripcd_tty_trap[num]->scan(buf,n);
      }
    }
  }
}


void MainObject::ExecCart(int cartnum)
{
  RDMacro rml;
  rml.setRole(RDMacro::Cmd);
  rml.setCommand(RDMacro::EX);
  rml.setAddress(rda->station()->address());
  rml.setEchoRequested(false);
  rml.addArg(cartnum);
  sendRml(&rml);
}


void MainObject::LogGpioEvent(int matrix,int line,RDMatrix::GpioType type,
			      bool state)
{
  QString sql;

  sql=QString("insert into `GPIO_EVENTS` set ")+
    "`STATION_NAME`='"+RDEscapeString(rda->station()->name())+"',"+
    QString::asprintf("`MATRIX`=%d,",matrix)+
    QString::asprintf("`NUMBER`=%d,",line+1)+
    QString::asprintf("`TYPE`=%d,",type)+
    QString::asprintf("`EDGE`=%d,",state)+
    "`EVENT_DATETIME`=now()";
  RDSqlQuery::apply(sql);
}


void MainObject::LoadLocalMacros()
{
  QString sql;
  RDSqlQuery *q;
  unsigned tty_port;
  QString cmd;

  for(int i=0;i<MAX_MATRICES;i++) {
    ripcd_switcher_tty[i][0]=-1;
    ripcd_switcher_tty[i][1]=-1;
  }
  for(int i=0;i<MAX_TTYS;i++) {
    ripcd_tty_inuse[i]=false;
    ripcd_tty_dev[i]=NULL;
  }

  //
  // Initialize Matrices
  //
  sql=QString("select ")+
    "`MATRIX`,"+   // 00
    "`TYPE`,"+     // 01
    "`PORT`,"+     // 02
    "`INPUTS`,"+   // 03
    "`OUTPUTS` "+  // 04
    "from `MATRICES` where "+
    "`STATION_NAME`='"+RDEscapeString(rda->station()->name())+"'";
  q=new RDSqlQuery(sql);
  while(q->next()) {
    if(!LoadSwitchDriver(q->value(0).toInt())) {
      rda->syslog(LOG_WARNING,
		  "attempted to load unknown switcher driver for matrix %d",
		  q->value(0).toInt());
    }
  }
  delete q;

  //
  // Initialize TTYs
  //
  sql=QString("select ")+
    "`PORT_ID`,"+      // 00
    "`PORT`,"+         // 01
    "`BAUD_RATE`,"+    // 02
    "`DATA_BITS`,"+    // 03
    "`PARITY`,"+       // 04
    "`TERMINATION` "+  // 05
    "from `TTYS` where "+
    "(`STATION_NAME`='"+RDEscapeString(rda->station()->name())+"')&&"+
    "(`ACTIVE`='Y')";
  q=new RDSqlQuery(sql);
  while(q->next()) {
    tty_port=q->value(0).toUInt();
    if(!ripcd_tty_inuse[tty_port]) {
      ripcd_tty_dev[tty_port]=new RDTTYDevice();
      ripcd_tty_dev[tty_port]->setName(q->value(1).toString());
      ripcd_tty_dev[tty_port]->setSpeed(q->value(2).toInt());
      ripcd_tty_dev[tty_port]->setWordLength(q->value(3).toInt());
      ripcd_tty_dev[tty_port]->
	setParity((RDTTYDevice::Parity)q->value(4).toInt());
      if(ripcd_tty_dev[tty_port]->open(QIODevice::ReadWrite)) {
	connect(ripcd_tty_dev[tty_port],SIGNAL(readyRead()),
		ripcd_tty_ready_read_mapper,SLOT(map()));
	ripcd_tty_ready_read_mapper->
	  setMapping(ripcd_tty_dev[tty_port],tty_port);
	ripcd_tty_term[tty_port]=(RDTty::Termination)q->value(5).toInt();
	ripcd_tty_inuse[tty_port]=true;
	ripcd_tty_trap[tty_port]=new RDCodeTrap(this);
	connect(ripcd_tty_trap[tty_port],SIGNAL(trapped(int)),
		this,SLOT(ttyTrapData(int)));
      }
      else {
	delete ripcd_tty_dev[tty_port];
	ripcd_tty_dev[tty_port]=NULL;
      }
    }
  }
  delete q;
}


void MainObject::RunLocalMacros(RDMacro *rml_in)
{
  int matrix_num;
  int gpi;
  int tty_port;
  int severity=0;
  QString str;
  QString sql;
  QString cmd;
  RDRange *range=NULL;
  RDSqlQuery *q;
  QHostAddress addr;
  RDUser *rduser;
  QString logstr;
  char bin_buf[RD_RML_MAX_LENGTH];
  int d;
  RDMatrix::GpioType gpio_type;
  QByteArray data;
  int err;

  rda->syslog(LOG_INFO,"received rml: \"%s\" from %s",
	      (const char *)rml_in->toString().toUtf8(),
	      (const char *)rml_in->address().toString().toUtf8());

  //  RDMacro *rml=new RDMacro();
  //  *rml=ForwardConvert(*rml_in);
  RDMacro rml=ForwardConvert(*rml_in);

  switch(rml.command()) {
  case RDMacro::BO:
    tty_port=rml.arg(0).toInt();
    if((tty_port<0)||(tty_port>MAX_TTYS)) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
	return;
      }
    }
    if(ripcd_tty_dev[tty_port]==NULL) {
      rml.acknowledge(false);
      sendRml(&rml);
      return;
    }
    for(int i=1;i<(rml.argQuantity());i++) {
      d=rml.arg(i).toInt(NULL,16);
      bin_buf[i-1]=0xFF&d;
    }
    ripcd_tty_dev[tty_port]->write(bin_buf,rml.argQuantity()-1);
    rml.acknowledge(true);
    sendRml(&rml);
    return;
    break;
      
  case RDMacro::GI:
    if(rml.argQuantity()!=5) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    matrix_num=rml.arg(0).toInt();
    if(rml.arg(1).toLower()=="i") {
      gpio_type=RDMatrix::GpioInput;
    }
    else {
      if(rml.arg(1).toLower()=="o") {
	gpio_type=RDMatrix::GpioOutput;
      }
      else {
	if(rml.echoRequested()) {
	  rml.acknowledge(false);
	  sendRml(&rml);
	}
	return;
      }
    }
    gpi=rml.arg(2).toInt()-1;
    if((ripcd_switcher[matrix_num]==NULL)||
       (gpi>(MAX_GPIO_PINS-1))||
       (gpi<0)||
       (rml.arg(3).toInt()<0)||(rml.arg(3).toInt()>1)||
       (rml.arg(4).toInt()<-1)||(rml.arg(4).toInt()>999999)) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    switch(gpio_type) {
    case RDMatrix::GpioInput:
      ripcd_gpi_macro[matrix_num][gpi][rml.arg(3).toInt()]=
	rml.arg(4).toInt();
      BroadcastCommand(QString::asprintf("GC %d %d %d %d!",matrix_num,gpi,
					 ripcd_gpi_macro[matrix_num][gpi][0],
					 ripcd_gpi_macro[matrix_num][gpi][1]));
      break;

    case RDMatrix::GpioOutput:
      ripcd_gpo_macro[matrix_num][gpi][rml.arg(3).toInt()]=
	rml.arg(4).toInt();
      BroadcastCommand(QString::asprintf("GD %d %d %d %d!",matrix_num,gpi,
					 ripcd_gpo_macro[matrix_num][gpi][0],
					 ripcd_gpo_macro[matrix_num][gpi][1]));
      break;
    }
    if(rml.echoRequested()) {
      rml.acknowledge(true);
      sendRml(&rml);
    }
    break;
      
  case RDMacro::GE:
    if(rml.argQuantity()!=4) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    matrix_num=rml.arg(0).toInt();
    if((ripcd_switcher[matrix_num]==NULL)||
       (rml.arg(3).toInt()<0)||(rml.arg(3).toInt()>1)) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if(rml.arg(1).toLower()=="i") {
      gpio_type=RDMatrix::GpioInput;
      range=new RDRange(ripcd_switcher[matrix_num]->gpiQuantity());
    }
    else {
      if(rml.arg(1).toLower()=="o") {
	gpio_type=RDMatrix::GpioOutput;
	range=new RDRange(ripcd_switcher[matrix_num]->gpoQuantity());
      }
      else {
	if(rml.echoRequested()) {
	  rml.acknowledge(false);
	  sendRml(&rml);
	}
	return;
      }
    }
    if(!range->parse(rml.arg(2))) {
      delete range;
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    for(int i=range->start()-1;i<range->end();i++) {
      switch(gpio_type) {
      case RDMatrix::GpioInput:
	if(rml.arg(3).toInt()==1) {
	  ripcd_gpi_mask[matrix_num][i]=true;
	  BroadcastCommand(QString::asprintf("GM %d %d 1!",matrix_num,i));
	}
	else {
	  ripcd_gpi_mask[matrix_num][i]=false;
	  BroadcastCommand(QString::asprintf("GM %d %d 0!",matrix_num,i));
	}
	break;

      case RDMatrix::GpioOutput:
	if(rml.arg(3).toInt()==1) {
	  ripcd_gpo_mask[matrix_num][i]=true;
	  BroadcastCommand(QString::asprintf("GN %d %d 1!",matrix_num,i));
	}
	else {
	  ripcd_gpo_mask[matrix_num][i]=false;
	  BroadcastCommand(QString::asprintf("GN %d %d 0!",matrix_num,i));
	}
	break;
      }
    }
    if(rml.echoRequested()) {
      rml.acknowledge(true);
      sendRml(&rml);
    }
    break;

  case RDMacro::JC:
#ifdef JACK
    if(rml.argQuantity()!=2) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if(ripcd_jack_client!=NULL) {
      if((err=jack_connect(ripcd_jack_client,rml.arg(1).toUtf8(),
			   rml.arg(0).toUtf8()))==0) {
	rda->syslog(LOG_DEBUG,
		    "executed JACK port connection \"%s %s\"",
		    (const char *)rml.arg(0).toUtf8(),
		    (const char *)rml.arg(1).toUtf8());
      }
      else {
	if(err!=EEXIST) {
	  rda->syslog(LOG_WARNING,
		      "JACK port connection \"%s %s\" failed, err: %d",
		      (const char *)rml.arg(0).toUtf8(),
		      (const char *)rml.arg(1).toUtf8(),
		      err);
	}
      }
    }
    else {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if(rml.echoRequested()) {
      rml.acknowledge(true);
      sendRml(&rml);
    }
#else
    if(rml.echoRequested()) {
      rml.acknowledge(false);
      sendRml(&rml);
    }
#endif  // JACK
    break;

  case RDMacro::JD:
#ifdef JACK
    if(rml.argQuantity()!=2) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if(ripcd_jack_client!=NULL) {
      if((err=jack_disconnect(ripcd_jack_client,rml.arg(1).toUtf8(),
			   rml.arg(0).toUtf8()))==0) {
	rda->syslog(LOG_DEBUG,
		    "executed JACK port disconnection \"%s %s\"",
		    (const char *)rml.arg(0).toUtf8(),
		    (const char *)rml.arg(1).toUtf8());
      }
      else {
	rda->syslog(LOG_WARNING,
		    "JACK port disconnection \"%s %s\" failed, err: %d",
		    (const char *)rml.arg(0).toUtf8(),
		    (const char *)rml.arg(1).toUtf8(),
		    err);
      }
    }
    else {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if(rml.echoRequested()) {
      rml.acknowledge(true);
      sendRml(&rml);
    }
#else
    if(rml.echoRequested()) {
      rml.acknowledge(false);
      sendRml(&rml);
    }
#endif  // JACK
    break;
      
  case RDMacro::JZ:
#ifdef JACK
    if(rml.argQuantity()!=0) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if(ripcd_jack_client!=NULL) {
      const char **outputs=jack_get_ports(ripcd_jack_client,NULL,
					JACK_DEFAULT_AUDIO_TYPE,
					JackPortIsOutput);
      if(outputs) {
	int i=0;
	while(outputs[i]!=0) {
	  jack_port_t *oport=jack_port_by_name(ripcd_jack_client,outputs[i]);
	  const char **inputs=
	    jack_port_get_all_connections(ripcd_jack_client,oport);
	  if(inputs) {
	    int j=0;
	    while(inputs[j]!=0) {
	      if((err=jack_disconnect(ripcd_jack_client,outputs[i],inputs[j]))==0) {
		rda->syslog(LOG_DEBUG,
			    "executed JACK port disconnection \"%s %s\"",
			    inputs[j],outputs[i]);
	      }
	      else {
		rda->syslog(LOG_WARNING,
			    "JACK port disconnection \"%s %s\" failed, err: %d",
			    inputs[j],outputs[i],err);
	      }
	      j++;
	    }
	  }
	  i++;
	}
      }
      if(rml.echoRequested()) {
	rml.acknowledge(true);
	sendRml(&rml);
      }
    }
#else
    if(rml.echoRequested()) {
      rml.acknowledge(false);
      sendRml(&rml);
    }
#endif  // JACK
    break;

  case RDMacro::LO:
    if(rml.argQuantity()>2) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if(rml.argQuantity()==0) {
      rduser=new RDUser(rda->station()->defaultName());
    }
    else {
      rduser=new RDUser(rml.arg(0));
      if(!rduser->exists()) {
	if(rml.echoRequested()) {
	  rml.acknowledge(false);
	  sendRml(&rml);
	}
	delete rduser;
	return;
      }
      if(!rduser->checkPassword(rml.arg(1),false)) {
	if(rml.echoRequested()) {
	  rml.acknowledge(false);
	  sendRml(&rml);
	}
	delete rduser;
	return;
      }
    }
    SetUser(rduser->name());
    if(rml.echoRequested()) {
      rml.acknowledge(true);
      sendRml(&rml);
    }
    delete rduser;
    break;
      
  case RDMacro::MB:
    if(rml.argQuantity()<3) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    severity=rml.arg(1).toInt();
    if((severity<1)||(severity>3)) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if(fork()==0) {
      if(getuid()==0) {
	if(setegid(rda->config()->gid())<0) {
	  rda->syslog(LOG_WARNING,"unable to set group id %d for RDPopup",
		      rda->config()->gid());
	  if(rml.echoRequested()) {
	    rml.acknowledge(false);
	    sendRml(&rml);
	  }
	}
	if(seteuid(rda->config()->uid())<0) {
	  rda->syslog(LOG_WARNING,"unable to set user id %d for RDPopup",
		      rda->config()->uid());
	  if(rml.echoRequested()) {
	    rml.acknowledge(false);
	    sendRml(&rml);
	  }
	}
      }
      if(system(QString().
		sprintf("rdpopup -display %s %s %s",
			rml.arg(0).toUtf8().constData(),
			rml.arg(1).toUtf8().constData(),
			RDEscapeString(rml.rollupArgs(2)).toUtf8().constData()).toUtf8().constData())<0) {
	rda->syslog(LOG_WARNING,"RDPopup returned an error");
      }
      exit(0);
    }
    if(rml.echoRequested()) {
      rml.acknowledge(true);
      sendRml(&rml);
    }
    break;

  case RDMacro::MT:
    if(rml.argQuantity()!=3) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if((rml.arg(0).toUInt()==0)||
       (rml.arg(0).toUInt()>RD_MAX_MACRO_TIMERS)||
       (rml.arg(2).toUInt()>999999)) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if((rml.arg(1).toUInt()==0)||
       (rml.arg(2).toUInt()==0)) {
      ripc_macro_cart[rml.arg(0).toUInt()-1]=0;
      ripc_macro_timer[rml.arg(0).toUInt()-1]->stop();
      if(rml.echoRequested()) {
	rml.acknowledge(true);
	sendRml(&rml);
      }
      return;
    }
    ripc_macro_cart[rml.arg(0).toUInt()-1]=rml.arg(2).toUInt();
    ripc_macro_timer[rml.arg(0).toUInt()-1]->stop();
    ripc_macro_timer[rml.arg(0).toUInt()-1]->
      start(rml.arg(1).toInt());
    if(rml.echoRequested()) {
      rml.acknowledge(true);
      sendRml(&rml);
    }
    return;
      
  case RDMacro::RN:
    if(rml.argQuantity()<1) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    for(int i=0;i<rml.argQuantity();i++) {
      cmd+=rml.arg(i)+" ";
    }
    RunCommand(rda->config()->rnRmlUid(),rda->config()->rnRmlGid(),
	       cmd.trimmed());
    if(rml.echoRequested()) {
      rml.acknowledge(true);
      sendRml(&rml);
    }
    break;

  case RDMacro::SI:
    tty_port=rml.arg(0).toInt();
    if((tty_port<0)||(tty_port>MAX_TTYS)||(rml.argQuantity()!=3)) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if(ripcd_tty_dev[tty_port]==NULL) {
      rml.acknowledge(false);
      sendRml(&rml);
      return;
    }
    for(int i=2;i<(rml.argQuantity()-1);i++) {
      str+=(rml.arg(i)+" ");
    }
    str+=rml.arg(rml.argQuantity()-1);
    ripcd_tty_trap[tty_port]->addTrap(rml.arg(1).toInt(),
				      str.toUtf8(),str.toUtf8().length());
    rda->syslog(LOG_DEBUG,"added trap \"%s\" to tty port %d",
		(const char *)str.toUtf8(),rml.arg(1).toInt());
    rml.acknowledge(true);
    sendRml(&rml);
    return;
    break;
      
  case RDMacro::SC:
    tty_port=rml.arg(0).toInt();
    if((tty_port<0)||(tty_port>MAX_TTYS)) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
	return;
      }
    }
    if(ripcd_tty_dev[tty_port]==NULL) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    switch(rml.argQuantity()) {
    case 1:
      ripcd_tty_trap[tty_port]->clear();
      if(rml.echoRequested()) {
	rml.acknowledge(true);
	sendRml(&rml);
      }
      break;

    case 2:
      ripcd_tty_trap[tty_port]->removeTrap(rml.arg(1).toInt());
      if(rml.echoRequested()) {
	rml.acknowledge(true);
	sendRml(&rml);
      }
      break;
	  
    case 3:
      ripcd_tty_trap[tty_port]->removeTrap(rml.arg(1).toInt(),
					   rml.arg(2).toUtf8(),
					   rml.arg(2).toUtf8().length());
      if(rml.echoRequested()) {
	rml.acknowledge(true);
	sendRml(&rml);
      }
      break;
	  
    default:
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
      break;
    }
    break;

  case RDMacro::SO:
    tty_port=rml.arg(0).toInt();
    if((tty_port<0)||(tty_port>MAX_TTYS)) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
	return;
      }
    }
    if(ripcd_tty_dev[tty_port]==NULL) {
      rml.acknowledge(false);
      sendRml(&rml);
      return;
    }
    for(int i=1;i<(rml.argQuantity()-1);i++) {
      str+=(rml.arg(i)+" ");
    }
    str+=rml.arg(rml.argQuantity()-1);
    switch(ripcd_tty_term[tty_port]) {
    case RDTty::CrTerm:
      str+=QString::asprintf("\x0d");
      break;
      
    case RDTty::LfTerm:
      str+=QString::asprintf("\x0a");
      break;
      
    case RDTty::CrLfTerm:
      str+=QString::asprintf("\x0d\x0a");
      break;
      
    default:
      break;
    }
    data=RDStringToData(str);
    ripcd_tty_dev[tty_port]->write(data);
    rml.acknowledge(true);
    sendRml(&rml);
    return;
    break;

  case RDMacro::CL:
  case RDMacro::FS:
  case RDMacro::GO:
  case RDMacro::ST:
  case RDMacro::SA:
  case RDMacro::SD:
  case RDMacro::SG:
  case RDMacro::SR:
  case RDMacro::SL:
  case RDMacro::SX:
    if((rml.arg(0).toInt()<0)||(rml.arg(0).toInt()>=MAX_MATRICES)) {
      if(!rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if(ripcd_switcher[rml.arg(0).toInt()]==NULL) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
    }
    else {
      ripcd_switcher[rml.arg(0).toInt()]->processCommand(&rml);
    }
    break;
      
  case RDMacro::SY:
    if(rml.argQuantity()!=1) {
      return;
    }
    tty_port=rml.arg(0).toInt();
    if((tty_port<0)||(tty_port>=MAX_TTYS)) {
      return;
    }
      
    //
    // Shutdown TTY Port
    //
    if(ripcd_tty_dev[tty_port]!=NULL) {
      ripcd_tty_dev[tty_port]->close();
      ripcd_tty_ready_read_mapper->disconnect(ripcd_tty_dev[tty_port]);
      delete ripcd_tty_dev[tty_port];
      ripcd_tty_dev[tty_port]=NULL;
      ripcd_tty_inuse[tty_port]=false;
      delete ripcd_tty_trap[tty_port];
      ripcd_tty_trap[tty_port]=NULL;
    }
  
    //
    // Try to Restart
    //
    sql=QString("select ")+
      "`PORT_ID`,"+      // 00
      "`PORT`,"+         // 01
      "`BAUD_RATE`,"+    // 02
      "`DATA_BITS`,"+    // 03
      "`PARITY`,"+       // 04
      "`TERMINATION` "+  // 05
      "from `TTYS` where "+
      "(`STATION_NAME`='"+RDEscapeString(rda->station()->name())+"')&&"+
      "(`ACTIVE`='Y')&&"+
      QString::asprintf("(`PORT_ID`=%d)",tty_port);
    q=new RDSqlQuery(sql);
    if(q->first()) {
      if(!ripcd_tty_inuse[tty_port]) {
	ripcd_tty_dev[tty_port]=new RDTTYDevice();
	ripcd_tty_dev[tty_port]->setName(q->value(1).toString());
	ripcd_tty_dev[tty_port]->setSpeed(q->value(2).toInt());
	ripcd_tty_dev[tty_port]->setWordLength(q->value(3).toInt());
	ripcd_tty_dev[tty_port]->
	  setParity((RDTTYDevice::Parity)q->value(4).toInt());
	if(ripcd_tty_dev[tty_port]->open(QIODevice::ReadWrite)) {
	  ripcd_tty_term[tty_port]=(RDTty::Termination)q->value(5).toInt();
	  ripcd_tty_inuse[tty_port]=true;
	  ripcd_tty_trap[tty_port]=new RDCodeTrap(this);
	  connect(ripcd_tty_trap[tty_port],SIGNAL(trapped(int)),
		  this,SLOT(ttyTrapData(int)));
	}
	else {
	  delete ripcd_tty_dev[tty_port];
	  ripcd_tty_dev[tty_port]=NULL;
	}
      }
    }
    delete q;
    break;

  case RDMacro::SZ:
    if(rml.argQuantity()!=1) {
      return;
    }
    matrix_num=rml.arg(0).toInt();
    if((matrix_num<0)||(matrix_num>=MAX_MATRICES)) {
      return;
    }
      
    //
    // Shutdown the old switcher
    //
    for(int i=0;i<2;i++) {
      if(ripcd_switcher_tty[matrix_num][i]>-1) {
	ripcd_tty_inuse[ripcd_switcher_tty[matrix_num][i]]=false;
	ripcd_switcher_tty[matrix_num][i]=-1;
      }
    }
    delete ripcd_switcher[matrix_num];
    ripcd_switcher[matrix_num]=NULL;

    //
    // Startup the new
    //
    if(!LoadSwitchDriver(matrix_num)) {
      rda->syslog(LOG_WARNING,
		  "attempted to load unknown switcher driver for matrix %d",
		  matrix_num);
    }
    break;
    
  case RDMacro::TA:
    if((rml.argQuantity()!=1)||
       (rml.arg(0).toInt()<0)||(rml.arg(0).toInt()>1)) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if((rml.arg(0).toInt()==0)&&ripc_onair_flag) {
      BroadcastCommand("TA 0!");
      rda->syslog(LOG_INFO,"onair flag OFF");
    }
    if((rml.arg(0).toInt()==1)&&(!ripc_onair_flag)) {
      BroadcastCommand("TA 1!");
      rda->syslog(LOG_INFO,"onair flag ON");
    }
    ripc_onair_flag=rml.arg(0).toInt();
    if(rml.echoRequested()) {
      rml.acknowledge(true);
      sendRml(&rml);
    }
    break;

  case RDMacro::UO:
    if(rml.argQuantity()<3) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if(!addr.setAddress(rml.arg(0))) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    if((rml.arg(1).toInt()<0)||(rml.arg(1).toInt()>0xFFFF)) {
      if(rml.echoRequested()) {
	rml.acknowledge(false);
	sendRml(&rml);
      }
      return;
    }
    for(int i=2;i<(rml.argQuantity()-1);i++) {
      str+=(rml.arg(i)+" ");
    }
    str+=rml.arg(rml.argQuantity()-1);
    rda->syslog(LOG_INFO,"sending \"%s\" to %s:%d",(const char *)str.toUtf8(),
		(const char *)addr.toString().toUtf8(),rml.arg(1).toInt());
    data=RDStringToData(str);
    ripcd_rml_send->writeDatagram(data,addr,(uint16_t)(rml.arg(1).toInt()));
    if(rml.echoRequested()) {
      rml.acknowledge(true);
      sendRml(&rml);
    }
    break;

  default:
    break;
  }
}


RDMacro MainObject::ForwardConvert(const RDMacro &rml) const
{
  RDMacro ret;

  ret.setCommand(rml.command());
  ret.setRole(rml.role());
  ret.setAddress(rml.address());
  ret.setEchoRequested(rml.echoRequested());

  //
  // Convert old RML syntax to current forms
  //
  switch(rml.command()) {
  case RDMacro::GE:
    if(rml.argQuantity()==3) {
      ret.addArg(rml.arg(0));
      ret.addArg("I");
      ret.addArg(rml.arg(1));
      ret.addArg(rml.arg(2));
    }
    else {
      ret=rml;
    }
    break;

  case RDMacro::GI:
    if(rml.argQuantity()==3) {
      ret.addArg(rml.arg(0));
      ret.addArg("I");
      ret.addArg(rml.arg(1));
      ret.addArg(rml.arg(2));
      ret.addArg(0);
    }
    else {
      if(rml.argQuantity()==4) {
	ret.addArg(rml.arg(0));
	ret.addArg("I");
	ret.addArg(rml.arg(1));
	ret.addArg(rml.arg(2));
	ret.addArg(rml.arg(3));
      }
      else {
	ret=rml;
      }
    }
    break;

  case RDMacro::GO:
    if(rml.argQuantity()==4) {
      ret.addArg(rml.arg(0));
      ret.addArg("O");
      ret.addArg(rml.arg(1));
      ret.addArg(rml.arg(2));
      ret.addArg(rml.arg(3));
    }
    else {
      ret=rml;
    }
    break;

  default:
    ret=rml;
    break;
  }

  return ret;
}


void MainObject::RunCommand(uid_t uid,gid_t gid,const QString &cmd) const
{
  //
  // Maintainer's Note: Everything passed to execl() *must* be either in
  //                    local or heap storage. *Don't* pass (const char *)
  //                    references from Qt directly, as they will fall
  //                    out of scope!
  //

  struct passwd *pwd=NULL;
  struct group *grp=NULL;
  QString username="[unknown]";
  QString groupname="[unknown]";

  if(fork()==0) {
    if(getuid()==0) {
      if(setgid(gid)!=0) {
	rda->syslog(LOG_WARNING,"unable to set effective GID=%d [%s]",uid,
		    strerror(errno));
	return;
      }
      if(setuid(uid)!=0) {
	rda->syslog(LOG_WARNING,"unable to set effective UID=%d [%s]",uid,
		    strerror(errno));
	return;
      }
      if(setuid(uid)!=0) {
	rda->syslog(LOG_WARNING,"unable to set real UID=%d [%s]",uid,
		    strerror(errno));
	return;
      }
    }
    if((pwd=getpwuid(getuid()))!=NULL) {
      username=QString::fromUtf8(pwd->pw_name);
    }
    if((grp=getgrgid(getgid()))!=NULL) {
      groupname=QString::fromUtf8(grp->gr_name);
    }
    rda->syslog(LOG_DEBUG,"executing script \"%s\" as %s:%s",
		cmd.toUtf8().constData(),username.toUtf8().constData(),
		groupname.toUtf8().constData());
    int cmdlen=cmd.toUtf8().size()+4;
    char cmdstr[cmdlen];
    memset(cmdstr,0,cmdlen);  // These function take bytes, not characters!
    memcpy(cmdstr,cmd.toUtf8().constData(),cmd.toUtf8().size());
    execl("/bin/sh","/bin/sh","-c",cmdstr,(char *)NULL);
    rda->syslog(LOG_WARNING,"execution of \"%s\" failed [%s]",
		cmd.toUtf8().constData(),strerror(errno));
    exit(0);
  }
}
