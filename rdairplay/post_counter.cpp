// post_counter.cpp
//
// The post counter widget for Rivendell
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

#include <QGuiApplication>
#include <QPainter>

#include <rdapplication.h>

#include "colors.h"
#include "post_counter.h"

PostCounter::PostCounter(QWidget *parent)
  : RDPushButton(parent)
{
  post_running=false;
  post_time_format="hh:mm:ss";
  post_time=QTime();
  post_offset=0;
  post_offset_valid=false;

  //
  // Generate Palettes
  //
  post_idle_palette=QGuiApplication::palette();
  post_early_palette=
    QPalette(QColor(POSTPOINT_EARLY_COLOR),
	     QGuiApplication::palette().color(QPalette::Inactive,
					      QPalette::Background));
  post_ontime_palette=
    QPalette(QColor(POSTPOINT_ONTIME_COLOR),
	     QGuiApplication::palette().color(QPalette::Inactive,
					      QPalette::Background));
  post_late_palette=
    QPalette(QColor(POSTPOINT_LATE_COLOR),
	     QGuiApplication::palette().color(QPalette::Inactive,
					      QPalette::Background));

  post_offset = 0;
  UpdateDisplay();
}


QSize PostCounter::sizeHint() const
{
  return QSize(200,80);
}


QSizePolicy PostCounter::sizePolicy() const
{
  return QSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
}


void PostCounter::setPostPoint(QTime point,int offset,bool offset_valid,
			       bool running)
{
  post_time=point;
  post_offset=offset;
  post_offset_valid=offset_valid;
  post_running=running;
  post_set_time=QTime::currentTime();
  UpdateDisplay();
}


void PostCounter::tickCounter()
{
  if(!post_running) {
    UpdateDisplay();
  }
}


void PostCounter::setEnabled(bool state)
{
  QWidget::setEnabled(state);
  UpdateDisplay();
}


void PostCounter::setDisabled(bool state)
{
  setEnabled(!state);
}


void PostCounter::keyPressEvent(QKeyEvent *e)
{
  e->ignore();
}


void PostCounter::resizeEvent(QResizeEvent *e)
{
  setIconSize(QSize(size().width()-2,size().height()-2));
}


void PostCounter::UpdateDisplay()
{
  QColor color=QGuiApplication::palette().color(QPalette::Inactive,
						QPalette::Background);
  QColor text_color=QGuiApplication::palette().buttonText().color();
  QString str;
  QString point;
  QString state;
  QTime current_time=
    QTime::currentTime().addMSecs(rda->station()->timeOffset());
  int offset=post_offset;
  if(!post_running) {
    offset-=current_time.msecsTo(post_set_time);
  }

  if(isEnabled()&&(!post_time.isNull())) {
    point=tr("Next Timed Start")+" ["+rda->timeString(post_time)+"]";
    if(post_offset_valid) {
      if(offset<-POST_COUNTER_MARGIN) {
	state="-"+QTime(0,0,0).addMSecs(-offset).toString();
	setPalette(post_early_palette);
	color=POSTPOINT_EARLY_COLOR;
      }
      else {
	if(offset>POST_COUNTER_MARGIN) {
	  state="+"+QTime(0,0,0).addMSecs(offset).toString();
	  setPalette(post_late_palette);
	  color=POSTPOINT_LATE_COLOR;
	}
	else {
	  state=tr("On Time");
	  setPalette(post_ontime_palette);
	  color=POSTPOINT_ONTIME_COLOR;
	}
      }
      text_color=Qt::color1;
    }
    else {
      state="--------";
      setPalette(post_idle_palette);
    }
  }
  else {     // No postpoint/disabled
    point=tr("Next Timed Start [--:--:--]");
    state="--------";
    setPalette(post_idle_palette);
  }
  QPixmap pix(size().width()-2,size().height()-2);
  QPainter *p=new QPainter(&pix);
  p->fillRect(0,0,size().width()-2,size().height()-2,color);
  p->setPen(QColor(text_color));
  p->setFont(subLabelFont());
  p->drawText((size().width()-2-p->
	       fontMetrics().width(point))/2,32,point);
  p->setFont(bannerFont());
  p->drawText((size().width()-2-p->
	       fontMetrics().width(state))/2,58,state);
  p->end();
  delete p;
  setIcon(pix);    
}
