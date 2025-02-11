// local_macros.cpp
//
// Local RML Macros for the Rivendell's RDAirPlay
//
//   (C) Copyright 2002-2022 Fred Gleason <fredg@paravelsystems.com>
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

#include <rdapplication.h>
#include <rddb.h>
#include <rdescape_string.h>
#include <rdmacro.h>

#include "button_log.h"
#include "rdairplay.h"

void MainWidget::RunLocalMacros(RDMacro *rml)
{
  QString str;
  QString logname;
  RDAirPlayConf::PanelType panel_type;
  int panel_number;
  QString sql;
  QPalette pal;
  bool ret=false;
  int fade;
  RDLogLine *logline=NULL;
  QString label;
  int mach=0;
  RDLogLine::TransType trans=RDLogLine::Play;
  int offset=0;
  bool ok=false;
  unsigned cart=0;

  if(rml->role()!=RDMacro::Cmd) {
    return;
  }

  switch(rml->command()) {
  case RDMacro::LB:     // Label
    if(rml->argQuantity()==0) {
      air_top_strip->messageWidget()->clear();
    }
    else {
      for(int i=0;i<(rml->argQuantity()-1);i++) {
	str+=(rml->arg(i)+" ");
      }
      str+=rml->arg(rml->argQuantity()-1);
      pal=air_top_strip->messageWidget()->palette();
      pal.setColor(QPalette::Active,QPalette::Foreground,QColor(Qt::black));
      pal.setColor(QPalette::Inactive,QPalette::Foreground,
		   QColor(Qt::black));
      air_top_strip->messageWidget()->setPalette(pal);
      air_top_strip->messageWidget()->setFont(MessageFont(str));
      air_top_strip->messageWidget()->setText(str);
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::LC:     // Color Label
    if(rml->argQuantity()<=1) {
      air_top_strip->messageWidget()->clear();
    }
    else {
      QColor color(rml->arg(0));
      if(!color.isValid()) {
	color=QColor(Qt::black);
      }
      for(int i=1;i<(rml->argQuantity()-1);i++) {
	str+=(rml->arg(i)+" ");
      }
      str+=rml->arg(rml->argQuantity()-1);
      pal=air_top_strip->messageWidget()->palette();
      pal.setColor(QPalette::Active,QPalette::Foreground,color);
      pal.setColor(QPalette::Inactive,QPalette::Foreground,color);
      air_top_strip->messageWidget()->setPalette(pal);
      air_top_strip->messageWidget()->setFont(MessageFont(str));
      air_top_strip->messageWidget()->setText(str);
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::LL:    // Load Log
    if((rml->argQuantity()<1)||(rml->argQuantity()>3)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(0).toInt()<1)||(rml->arg(0).toInt()>3)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(rml->argQuantity()==1) {   // Clear Log
      air_log[rml->arg(0).toInt()-1]->clear();
    }
    else {  // Load Log
      logname=rml->arg(1);
      if(!RDLog::exists(logname)) {
	rda->syslog(LOG_WARNING,"unable to load log \"%s\", no such log",
		    logname.toUtf8().constData());
	if(rml->echoRequested()) {
	  rml->acknowledge(false);
	  rda->ripc()->sendRml(rml);
	}
	return;
      }
      air_log[rml->arg(0).toInt()-1]->setLogName(logname);
      air_log[rml->arg(0).toInt()-1]->load();
    }
    if(rml->argQuantity()==3) { // Start Log
      if(rml->arg(2).toInt()<air_log[rml->arg(0).toInt()-1]->lineCount()) {
	if(rml->arg(2).toInt()>=0) {  // Unconditional start
	  air_log[rml->arg(0).toInt()-1]->play(rml->arg(2).toInt(),
					       RDLogLine::StartMacro);
	}
	if(rml->arg(2).toInt()==-2) {  // Start if trans type allows
	  // Find first non-running event
	  bool found=false;
	  for(int i=0;i<air_log[rml->arg(0).toInt()-1]->lineCount();i++) {
	    if((logline=air_log[rml->arg(0).toInt()-1]->logLine(i))!=NULL) {
	      if(logline->status()==RDLogLine::Scheduled) {
		found=true;
		i=air_log[rml->arg(0).toInt()-1]->lineCount();
	      }
	    }
	  }
	  if(found) {
	    switch(logline->transType()) {
	    case RDLogLine::Play:
	    case RDLogLine::Segue:
	      air_log[rml->arg(0).toInt()-1]->
		play(0,RDLogLine::StartMacro);
	      break;
	      
	    case RDLogLine::Stop:
	    case RDLogLine::NoTrans:
	      break;
	    }
	  }
	}
      }
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::AL:   // Append Log
    if(rml->argQuantity()!=2) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(0).toInt()<1)||(rml->arg(0).toInt()>3)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    logname=rml->arg(1);
    if(!RDLog::exists(logname)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    air_log[rml->arg(0).toInt()-1]->append(logname);
    break;

  case RDMacro::MN:    // Make Next
    if(rml->argQuantity()!=2) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(0).toInt()<1)||(rml->arg(0).toInt()>3)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(1).toInt()<0)||
       (rml->arg(1).toInt()>=air_log[rml->arg(0).toInt()-1]->lineCount())) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    air_log[rml->arg(0).toInt()-1]->makeNext(rml->arg(1).toInt());
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::PB:   // Push Button
    if(rml->argQuantity()!=1) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(0).toInt()<1)||
       (rml->arg(0).toInt()>BUTTON_TOTAL_BUTTONS)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
	return;
      }
    }
    air_button_list->startButton(rml->arg(0).toInt()-1);
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::PC:   // Label Button
    if(rml->argQuantity()<5) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(!GetPanel(rml->arg(0),&panel_type,&panel_number)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(1).toInt()<=0)||
       (rml->arg(1).toInt()>PANEL_MAX_BUTTON_COLUMNS)||
       (rml->arg(2).toInt()<=0)||
       (rml->arg(2).toInt()>PANEL_MAX_BUTTON_ROWS)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    for(int i=3;i<(rml->argQuantity()-1);i++) {
      label+=(rml->arg(i)+" ");
    }
    label=label.left(label.length()-1);
    air_panel->soundPanelWidget()->
      setText(panel_type,panel_number,rml->arg(2).toInt()-1,
	      rml->arg(1).toInt()-1,label);
    air_panel->soundPanelWidget()->
      setColor(panel_type,panel_number,rml->arg(2).toInt()-1,
	       rml->arg(1).toInt()-1,
	       rml->arg(rml->argQuantity()-1));
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::PE:    // Load Panel Button
    if(rml->argQuantity()!=4) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(!GetPanel(rml->arg(0),&panel_type,&panel_number)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(1).toInt()<=0)||
       (rml->arg(1).toInt()>PANEL_MAX_BUTTON_COLUMNS)||
       (rml->arg(2).toInt()<=0)||
       (rml->arg(2).toInt()>PANEL_MAX_BUTTON_ROWS)||
       (rml->arg(3).toUInt()>RD_MAX_CART_NUMBER)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    air_panel->soundPanelWidget()->
      setButton(panel_type,panel_number,rml->arg(2).toInt()-1,
		rml->arg(1).toInt()-1,rml->arg(3).toUInt());
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::PL:    // Start
    if(rml->argQuantity()!=2) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(0).toInt()<1)||(rml->arg(0).toInt()>3)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(1).toInt()<0)||
       (rml->arg(1).toInt()>=air_log[rml->arg(0).toInt()-1]->lineCount())) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(!air_log[rml->arg(0).toInt()-1]->running()) {
      if(!air_log[rml->arg(0).toInt()-1]->play(rml->arg(1).toInt(),
					       RDLogLine::StartMacro)) {
	if(rml->echoRequested()) {
	  rml->acknowledge(false);
	  rda->ripc()->sendRml(rml);
	}
	return;
      }
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::PM:    // Set Mode
    if((rml->argQuantity()!=1)&&(rml->argQuantity()!=2)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(rml->argQuantity()==2) {
      mach=rml->arg(1).toInt();
      if((mach<0)||(mach>RDAIRPLAY_LOG_QUANTITY)) {
	if(rml->echoRequested()) {
	  rml->acknowledge(false);
	  rda->ripc()->sendRml(rml);
	}
	return;
      }
    }
    switch((RDAirPlayConf::OpMode)rml->arg(0).toInt()) {
    case RDAirPlayConf::LiveAssist:
      SetLiveAssistMode(mach-1);
      break;

    case RDAirPlayConf::Manual:
      SetManualMode(mach-1);
      break;

    case RDAirPlayConf::Auto:
      SetAutoMode(mach-1);
      break;

    default:
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
	return;
      }
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::PN:    // Start Next
    if((rml->argQuantity()<1)||(rml->argQuantity()>3)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(0).toInt()<1)||(rml->arg(0).toInt()>3)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(rml->argQuantity()>=2) {
      if((rml->arg(1).toInt()<1)||(rml->arg(1).toInt()>2)) {
	if(rml->echoRequested()) {
	  rml->acknowledge(false);
	  rda->ripc()->sendRml(rml);
	}
	return;
      }
      if(rml->argQuantity()==3) {
	if((rml->arg(2).toInt()<0)||(rml->arg(2).toInt()>1)) {
	  if(rml->echoRequested()) {
	    rml->acknowledge(false);
	    rda->ripc()->sendRml(rml);
	  }
	  return;
	}
      }
    }
    if(air_log[rml->arg(0).toInt()-1]->nextLine()>=0) {
      if(rml->argQuantity()==1) {
	air_log[rml->arg(0).toInt()-1]->
	  play(air_log[rml->arg(0).toInt()-1]->nextLine(),
	       RDLogLine::StartMacro);
      }
      else {
	if(rml->argQuantity()==2) {
	  air_log[rml->arg(0).toInt()-1]->
	    play(air_log[rml->arg(0).toInt()-1]->nextLine(),
		 RDLogLine::StartMacro,rml->arg(1).toInt()-1);
	}
	else {
	  air_log[rml->arg(0).toInt()-1]->
	    play(air_log[rml->arg(0).toInt()-1]->nextLine(),
		 RDLogLine::StartMacro,
		 rml->arg(1).toInt()-1,rml->arg(2).toInt());
	}
      }
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::PP:    // Play Panel Button
    if(rml->argQuantity()<3 || rml->argQuantity()>5) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(!GetPanel(rml->arg(0),&panel_type,&panel_number)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(1).toInt()<0)||
       (rml->arg(1).toInt()>PANEL_MAX_BUTTON_COLUMNS)||
       (rml->arg(2).toInt()<0)||
       (rml->arg(2).toInt()>PANEL_MAX_BUTTON_ROWS)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    switch(rml->argQuantity()) {
    case 3:
      air_panel->soundPanelWidget()->
	play(panel_type,panel_number,rml->arg(2).toInt()-1,
	     rml->arg(1).toInt()-1,RDLogLine::StartMacro);
      break; 

    case 4:
      air_panel->soundPanelWidget()->
	play(panel_type,panel_number,rml->arg(2).toInt()-1,
	     rml->arg(1).toInt()-1,RDLogLine::StartMacro,rml->arg(3).toInt());
      break;
 
    case 5: 
      if(rml->arg(4).toInt()==1) {
	air_panel->soundPanelWidget()->
	  play(panel_type,panel_number,rml->arg(2).toInt()-1,
	       rml->arg(1).toInt()-1,
	       RDLogLine::StartMacro,rml->arg(3).toInt(),true);
      }
      else {
	air_panel->soundPanelWidget()->
	  play(panel_type,panel_number,rml->arg(2).toInt()-1,
	       rml->arg(1).toInt()-1,RDLogLine::StartMacro,rml->arg(3).toInt());
      }
      break;

    default:
      break;
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::PS:    // Stop
    if(rml->argQuantity()<1 || rml->argQuantity()>3) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(0).toInt()<0)||(rml->arg(0).toInt()>3)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    fade=0;
    if(rml->argQuantity()>1) {
      fade=rml->arg(1).toInt();
    }
    switch(rml->arg(0).toInt()) {
    case 0:   // Stop All Logs
      air_log[0]->stop(true,0,fade);
      air_log[1]->stop(true,0,fade);
      air_log[2]->stop(true,0,fade);
      break;

    case 1:
    case 2:
    case 3:
      if(rml->argQuantity()==3) {
	air_log[rml->arg(0).toInt()-1]->stop(false,rml->arg(2).toInt(),fade);
      }
      else {
	air_log[rml->arg(0).toInt()-1]->stop(true,0,fade);
      }
      break;
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::MD:    // Duck Machine
    if(rml->argQuantity()<3 || rml->argQuantity()>4) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(0).toInt()<0)||(rml->arg(0).toInt()>3)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    switch(rml->arg(0).toInt()) {
    case 0:   // Duck All Logs
      air_log[0]->duckVolume(rml->arg(1).toInt()*100,rml->arg(2).toInt());
      air_log[1]->duckVolume(rml->arg(1).toInt()*100,rml->arg(2).toInt());
      air_log[2]->duckVolume(rml->arg(1).toInt()*100,rml->arg(2).toInt());
      break;

    case 1:
    case 2:
    case 3:
      if(rml->argQuantity()==3) {
	air_log[rml->arg(0).toInt()-1]->duckVolume(rml->arg(1).toInt()*100,rml->arg(2).toInt());
      }
      else {
	air_log[rml->arg(0).toInt()-1]->duckVolume(rml->arg(1).toInt()*100,
						   rml->arg(2).toInt(),rml->arg(3).toInt());
      }
      break;
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::PT:    // Stop Panel Button
    if(rml->argQuantity()<3 || rml->argQuantity()>6) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(!GetPanel(rml->arg(0),&panel_type,&panel_number)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(1).toInt()<0)||
       (rml->arg(1).toInt()>PANEL_MAX_BUTTON_COLUMNS)||
       (rml->arg(2).toInt()<0)||
       (rml->arg(2).toInt()>PANEL_MAX_BUTTON_ROWS)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    switch(rml->argQuantity()) {
    case 3: 
      air_panel->soundPanelWidget()->
	stop(panel_type,panel_number,rml->arg(2).toInt()-1,
	     rml->arg(1).toInt()-1);
      break;
  
    case 4:
      air_panel->soundPanelWidget()->
	stop(panel_type,panel_number,rml->arg(2).toInt()-1,
	     rml->arg(1).toInt()-1,rml->arg(3).toInt());
      break;

    case 5:
      if(rml->arg(4).toInt()==1) {
	air_panel->soundPanelWidget()->
	  stop(panel_type,panel_number,rml->arg(2).toInt()-1,
	       rml->arg(1).toInt()-1,rml->arg(3).toInt(),true);
      }
      else {
	air_panel->soundPanelWidget()->
	  stop(panel_type,panel_number,rml->arg(2).toInt()-1,
	       rml->arg(1).toInt()-1,rml->arg(3).toInt(),false);
      }
      break;
         
    case 6:
      if(rml->arg(4).toInt()==1) {
	air_panel->soundPanelWidget()->
	  stop(panel_type,panel_number,rml->arg(2).toInt()-1,
	       rml->arg(1).toInt()-1,rml->arg(3).toInt(),true,
	       rml->arg(5).toInt());
      }
      else {
	air_panel->soundPanelWidget()->
	  stop(panel_type,panel_number,rml->arg(2).toInt()-1,
	       rml->arg(1).toInt()-1,rml->arg(3).toInt(),false,
	       rml->arg(5).toInt());
      }
      break;
         
    default: 
      break;
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::PU:    // Pause Panel Button
    if(rml->argQuantity()<3 || rml->argQuantity()>4) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(!GetPanel(rml->arg(0),&panel_type,&panel_number)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(1).toInt()<0)||
       (rml->arg(1).toInt()>PANEL_MAX_BUTTON_COLUMNS)||
       (rml->arg(2).toInt()<0)||
       (rml->arg(2).toInt()>PANEL_MAX_BUTTON_ROWS)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(rml->argQuantity()==3) {
      ret=air_panel->soundPanelWidget()->
	pause(panel_type,panel_number,rml->arg(2).toInt()-1,
	      rml->arg(1).toInt()-1);
    }
    else {
      ret=air_panel->soundPanelWidget()->
	pause(panel_type,panel_number,rml->arg(2).toInt()-1,
	      rml->arg(1).toInt()-1,rml->arg(3).toInt());
    }
    if(rml->echoRequested()) {
      rml->acknowledge(ret);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::PD:    // Duck Panel Button
    if(rml->argQuantity()<5 || rml->argQuantity()>6) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(!GetPanel(rml->arg(0),&panel_type,&panel_number)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(1).toInt()<0)||
       (rml->arg(1).toInt()>PANEL_MAX_BUTTON_COLUMNS)||
       (rml->arg(2).toInt()<0)||
       (rml->arg(2).toInt()>PANEL_MAX_BUTTON_ROWS)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(rml->argQuantity()==5) {
      air_panel->soundPanelWidget()->
	duckVolume(panel_type,panel_number,rml->arg(2).toInt()-1,
		   rml->arg(1).toInt()-1,(rml->arg(3).toInt())*100,
		   rml->arg(4).toInt());
    }
    else {
      air_panel->soundPanelWidget()->
	duckVolume(panel_type,panel_number,rml->arg(2).toInt()-1,
		   rml->arg(1).toInt()-1,(rml->arg(3).toInt())*100,
		   rml->arg(4).toInt(),rml->arg(5).toInt());
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;


  case RDMacro::PW:    // Select Widget
    if(rml->argQuantity()!=1) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(0).toInt()<0)||(rml->arg(0).toInt()>4)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    switch(rml->arg(0).toInt()) {
    case 0:  // Sound Panel
      panelButtonData();
      break;

    case 1:
    case 2:
    case 3:
      fullLogButtonData(rml->arg(0).toInt()-1);
      break;

    case 4:  // Voice Tracker
      if(rda->user()->voicetrackLog()) {
	trackerButtonData();
      }
      else {
	if(rml->echoRequested()) {
	  rml->acknowledge(false);
	  rda->ripc()->sendRml(rml);
	}
	return;
      }
      break;

    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::PX:    // Add Next
    if((rml->argQuantity()<2)||(rml->argQuantity()>4)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    mach=rml->arg(0).toInt();
    cart=cart;
    offset=0;
    trans=air_default_trans_type;
    if((mach<1)||(mach>3)||
       (rml->arg(1).toUInt()>RD_MAX_CART_NUMBER)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(rml->argQuantity()>=3) {
      offset=rml->arg(2).toInt(&ok);
      if(!ok) {
	if(rml->echoRequested()) {
	  rml->acknowledge(false);
	  rda->ripc()->sendRml(rml);
	}
	return;
      }
    }
    if(rml->argQuantity()==4) {
      trans=RDLogLine::NoTrans;
      if(rml->arg(3).toLower()=="play") {
	trans=RDLogLine::Play;
      }
      if(rml->arg(3).toLower()=="segue") {
	trans=RDLogLine::Segue;
      }
      if(rml->arg(3).toLower()=="stop") {
	trans=RDLogLine::Stop;
      }
      if(trans==RDLogLine::NoTrans) {
	if(rml->echoRequested()) {
	  rml->acknowledge(false);
	  rda->ripc()->sendRml(rml);
	}
	return;
      }
    }
    if(air_log[mach-1]->nextLine()>=0) {
      air_log[mach-1]->
	insert(air_log[mach-1]->nextLine()+offset,
	       rml->arg(1).toUInt(),RDLogLine::NoTrans,trans);
    }
    else {
      air_log[mach-1]->
	insert(air_log[mach-1]->lineCount(),
	       rml->arg(1).toUInt(),RDLogLine::NoTrans,trans);
      air_log[mach-1]->
	makeNext(air_log[mach-1]->lineCount()-1);
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::RL:    // Refresh Log
    if(rml->argQuantity()!=1) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(0).toInt()<1)||(rml->arg(0).toInt()>3)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(!air_log[rml->arg(0).toInt()-1]->refresh()) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  case RDMacro::SN:    // Set default Now & Next Cart
    if(rml->argQuantity()!=3) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(0).toLower()!="now")&&
       (rml->arg(0).toLower()!="next")) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if((rml->arg(1).toInt()<1)||(rml->arg(1).toInt()>3)) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(rml->arg(2).toUInt()>RD_MAX_CART_NUMBER) {
      if(rml->echoRequested()) {
	rml->acknowledge(false);
	rda->ripc()->sendRml(rml);
      }
      return;
    }
    if(rml->arg(0).toLower()=="now") {
      air_log[rml->arg(1).toInt()-1]->setNowCart(rml->arg(2).toUInt());
    }
    else {
      air_log[rml->arg(1).toInt()-1]->setNextCart(rml->arg(2).toUInt());
    }
    if(rml->echoRequested()) {
      rml->acknowledge(true);
      rda->ripc()->sendRml(rml);
    }   
    break;

  default:
    break;
  }
}


QFont MainWidget::MessageFont(QString str)
{
  for(int i=(AIR_MESSAGE_FONT_QUANTITY-1);i>=0;i--) {
    if(air_message_metrics[i]->width(str)<MESSAGE_WIDGET_WIDTH) {
      return air_message_fonts[i];
    }
  }
  return air_message_fonts[0];
}


bool MainWidget::GetPanel(QString str,RDAirPlayConf::PanelType *type,
			  int *panel)
{
  bool ok=false;

  switch(str.at(0).cell()) {
      case 's':
      case 'S':
	*type=RDAirPlayConf::StationPanel;
	break;

      case 'u':
      case 'U':
	*type=RDAirPlayConf::UserPanel;
	  break;

      case 'c':
      case 'C':
	*type=air_panel->soundPanelWidget()->currentType();
        *panel=air_panel->soundPanelWidget()->currentNumber();
        return true; 
	  break;


      default:
	return false;
  }
  *panel=str.right(str.length()-1).toInt(&ok);
  if(!ok) {
    return false;
  }
  if((*panel<=0)||(*panel>rda->airplayConf()->panels(*type))) {
    return false;
  }
  (*panel)--;
  return true;
}
