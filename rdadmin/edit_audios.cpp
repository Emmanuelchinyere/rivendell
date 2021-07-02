// edit_audios.cpp
//
// Edit a Rivendell Audio Port Configuration
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

#include <QPushButton>
#include <QSignalMapper>

#include "edit_audios.h"

EditAudioPorts::EditAudioPorts(QString station,QWidget *parent)
  : RDDialog(parent)
{
  QString str;

  //
  // Fix the Window Size
  //
  setMaximumSize(sizeHint());
  setMinimumSize(sizeHint());

  edit_station=station;
  edit_card=NULL;
  rdstation=NULL;

  setWindowTitle("RDAdmin - "+tr("Edit Audio Ports"));

  //
  // Card Selector
  //
  edit_card_box=new QComboBox(this);
  connect(edit_card_box,SIGNAL(activated(int)),
	  this,SLOT(cardSelectedData(int)));
  edit_card_label=new QLabel(tr("Card:"),this);
  edit_card_label->setFont(labelFont());
  edit_card_label->setAlignment(Qt::AlignRight);

  //
  // Card Driver
  //
  card_driver_edit=new QLineEdit(this);
  card_driver_edit->setReadOnly(true);
  card_driver_label=new QLabel(tr("Card Driver:"),this);
  card_driver_label->setFont(labelFont());
  card_driver_label->setAlignment(Qt::AlignRight);

  //
  // Clock Selector
  //
  edit_clock_box=new QComboBox(this);
  edit_clock_label=new QLabel(tr("Clock Source:"),this);
  edit_clock_label->setFont(labelFont());
  edit_clock_label->setAlignment(Qt::AlignRight);

  for(int i=0;i<RD_MAX_PORTS;i++) {
    //
    // Input Port Controls
    //
    str=QString(tr("Input Port"));
    edit_inportnum_label[i]=new QLabel(str+QString().sprintf(" %d",i),this);
    edit_inportnum_label[i]->setFont(labelFont());
    edit_inportnum_label[i]->setAlignment(Qt::AlignHCenter);  
    QSignalMapper *mapper=new QSignalMapper(this);
    connect(mapper,SIGNAL(mapped(int)),this,SLOT(inputMapData(int)));
    edit_type_box[i]=new QComboBox(this);
    edit_type_box[i]->
      insertItem(edit_type_box[i]->count(),tr("Analog"));
    edit_type_box[i]->
      insertItem(edit_type_box[i]->count(),tr("AES/EBU"));
    edit_type_box[i]->
      insertItem(edit_type_box[i]->count(),tr("SP/DIFF"));
    mapper->setMapping(edit_type_box[i],i);
    connect(edit_type_box[i],SIGNAL(activated(int)),mapper,SLOT(map()));
    edit_type_label[i]=new QLabel(tr("Type:"),this);
    edit_type_label[i]->setFont(labelFont());
    edit_type_label[i]->setAlignment(Qt::AlignRight);
    edit_mode_box[i]=new QComboBox(this);
    // NOTE: this drop down list box is populated to match RDCae::ChannelMode
    edit_mode_box[i]->
      insertItem(edit_mode_box[i]->count(),tr("Normal"));
    edit_mode_box[i]->
      insertItem(edit_mode_box[i]->count(),tr("Swap"));
    edit_mode_box[i]->
      insertItem(edit_mode_box[i]->count(),tr("Left only"));
    edit_mode_box[i]->
      insertItem(edit_mode_box[i]->count(),tr("Right only"));
    mapper->setMapping(edit_mode_box[i],i);
    connect(edit_mode_box[i],SIGNAL(activated(int)),mapper,SLOT(map()));
    edit_mode_label[i]=new QLabel(tr("Mode:"),this);
    edit_mode_label[i]->setFont(labelFont());
    edit_mode_label[i]->setAlignment(Qt::AlignRight);

    edit_input_box[i]=new QSpinBox(this);
    edit_input_box[i]->setRange(-26,6);
    edit_input_box[i]->setSuffix(tr(" dB"));
    edit_input_label[i]=new QLabel(tr("Ref. Level:"),this);
    edit_input_label[i]->setFont(labelFont());
    edit_input_label[i]->setAlignment(Qt::AlignRight);
  
    //
    // Output Port Controls
    //
    str=QString(tr("Output Port"));
    edit_outportnum_label[i]=new QLabel(str+QString().sprintf(" %d",i),this);
    edit_outportnum_label[i]->setFont(labelFont());
    edit_outportnum_label[i]->setAlignment(Qt::AlignHCenter);  
      
    edit_output_box[i]=new QSpinBox(this);
    edit_output_box[i]->setRange(-26,6);
    edit_output_box[i]->setSuffix(tr(" dB"));
    edit_output_label[i]=new QLabel(tr("Ref. Level:"),this);
    edit_output_label[i]->setFont(labelFont());
    edit_output_label[i]->setAlignment(Qt::AlignRight);
  }

  //
  //  Help Button
  //
  edit_help_button=new QPushButton(this);
  edit_help_button->setFont(buttonFont());
  edit_help_button->setText(tr("Help"));
  connect(edit_help_button,SIGNAL(clicked()),this,SLOT(helpData()));

  //
  //  Close Button
  //
  edit_close_button=new QPushButton(this);
  edit_close_button->setFont(buttonFont());
  edit_close_button->setText(tr("Close"));
  connect(edit_close_button,SIGNAL(clicked()),this,SLOT(closeData()));

  //
  // Populate Data
  //
  for(int i=0;i<RD_MAX_PORTS;i++) {
    edit_card_box->insertItem(edit_card_box->count(),QString().sprintf("%d",i));
  }
  edit_clock_box->insertItem(edit_clock_box->count(),tr("Internal"));
  edit_clock_box->insertItem(edit_clock_box->count(),tr("AES/EBU Signal"));
  edit_clock_box->insertItem(edit_clock_box->count(),tr("SP/DIFF Signal"));
  edit_clock_box->insertItem(edit_clock_box->count(),tr("Word Clock"));
  edit_card_num=edit_card_box->currentIndex();
  ReadRecord(edit_card_num);
}


EditAudioPorts::~EditAudioPorts()
{
}


QSize EditAudioPorts::sizeHint() const
{
  return QSize(1420,652);
} 


QSizePolicy EditAudioPorts::sizePolicy() const
{
  return QSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
}


void EditAudioPorts::cardSelectedData(int card)
{
  WriteRecord();
  edit_card_num=edit_card_box->currentIndex();
  ReadRecord(edit_card_num);
}


void EditAudioPorts::inputMapData(int id)
{
  if(rdstation==NULL) {
    rdstation=new RDStation(edit_station);
  }
  if( (rdstation->cardDriver(edit_card_num)==RDStation::Hpi) &&
      (edit_type_box[id]->currentIndex()==RDAudioPort::Analog) ) {
    edit_input_label[id]->setEnabled(true);
    edit_input_box[id]->setEnabled(true);
  }
  else {
    edit_input_label[id]->setDisabled(true);
    edit_input_box[id]->setDisabled(true);
  }
}


void EditAudioPorts::helpData()
{
  HelpAudioPorts *help_audioports=new HelpAudioPorts(this);
  help_audioports->exec();
  delete help_audioports;
}


void EditAudioPorts::closeData()
{
  WriteRecord();
  done(0);
}


void EditAudioPorts::resizeEvent(QResizeEvent *e)
{
  printf("resizeEvent(%d,%d)\n",size().width(),size().height());
  //
  // Header
  //
  edit_card_box->setGeometry(75,10,60,26);
  edit_card_label->setGeometry(10,16,60,22);
  card_driver_edit->setGeometry(225,15,170,19);//FIXME: size
  card_driver_label->setGeometry(140,16,80,22);
  edit_clock_box->setGeometry(500,10,150,26);
  edit_clock_label->setGeometry(395,16,100,22);

  for(int i=0;i<RD_MAX_PORTS;i++) {
    int row=i/8;
    int col=i%8;

    //
    // Input Section
    //
    edit_inportnum_label[i]->setGeometry(50+170*col,55+row*180,170,22);
    edit_type_box[i]->setGeometry(95+170*col,75+row*180,110,26);
    edit_type_label[i]->setGeometry(50+170*col,81+row*180,40,22);
    edit_mode_box[i]->setGeometry(95+170*col,105+row*180,110,26);
    edit_mode_label[i]->setGeometry(50+170*col,111+row*180,40,22);
    edit_input_box[i]->setGeometry(95+170*col,135+row*180,60,24);
    edit_input_label[i]->setGeometry(10+170*col,140+row*180,80,22);

    //
    // Output Section
    //
    edit_outportnum_label[i]->setGeometry(50+170*col,170+row*180,170,22);
    edit_output_box[i]->setGeometry(95+170*col,190+row*180,60,24);
    edit_output_label[i]->setGeometry(10+170*col,195+row*180,80,22);
  }

  //
  // Bottom Row
  //
  edit_help_button->setGeometry(10,size().height()-60, 80,50);
  edit_close_button->setGeometry(size().width()-90,size().height()-60,80,50);
}


void EditAudioPorts::ReadRecord(int card)
{
  if(edit_card!=NULL) {
    delete edit_card;
  }
  edit_card=new RDAudioPort(edit_station,card);
  if(rdstation!=NULL) {
    delete rdstation;
  }
  rdstation=new RDStation(edit_station);
  card_driver_edit->setText(rdstation->name());

  // NOTE: various controls are disabled for some card driver types if they are not yet implemented within CAE.
  switch(rdstation->cardDriver(card)) {
      case RDStation::Hpi:
        card_driver_edit->setText("AudioScience HPI");
        edit_clock_box->setEnabled(true);
        edit_clock_label->setEnabled(true);
        for (int i=0;i<RD_MAX_PORTS;i++) {
          edit_type_label[i]->setEnabled(true);
          edit_type_box[i]->setEnabled(true);
          edit_mode_label[i]->setEnabled(true);
          edit_mode_box[i]->setEnabled(true);
          edit_input_label[i]->setEnabled(true);
          edit_input_box[i]->setEnabled(true);
          edit_output_label[i]->setEnabled(true);
          edit_output_box[i]->setEnabled(true);
        }
        break;
      case RDStation::Jack:
        card_driver_edit->setText("JACK");
        edit_clock_box->setDisabled(true);
        edit_clock_label->setDisabled(true);
	for (int i=0;i<RD_MAX_PORTS;i++) {
          edit_type_label[i]->setDisabled(true);
          edit_type_box[i]->setDisabled(true);
          edit_mode_label[i]->setEnabled(true);
          edit_mode_box[i]->setEnabled(true);
          edit_input_label[i]->setDisabled(true);
          edit_input_box[i]->setDisabled(true);
          edit_output_label[i]->setDisabled(true);
          edit_output_box[i]->setDisabled(true);
        }
        break;
      case RDStation::Alsa:
        card_driver_edit->setText("ALSA");
        edit_clock_box->setDisabled(true);
        edit_clock_label->setDisabled(true);
	for (int i=0;i<RD_MAX_PORTS;i++) {
          edit_type_label[i]->setDisabled(true);
          edit_type_box[i]->setDisabled(true);
          edit_mode_label[i]->setDisabled(true);
          edit_mode_box[i]->setDisabled(true);
          edit_input_label[i]->setDisabled(true);
          edit_input_box[i]->setDisabled(true);
          edit_output_label[i]->setDisabled(true);
          edit_output_box[i]->setDisabled(true);
        }
        break;
      case RDStation::None:
      default:
        card_driver_edit->setText("UNKNOWN");
        edit_clock_box->setDisabled(true);
        edit_clock_label->setDisabled(true);
	for (int i=0;i<RD_MAX_PORTS;i++) {
          edit_type_label[i]->setDisabled(true);
          edit_type_box[i]->setDisabled(true);
          edit_mode_label[i]->setDisabled(true);
          edit_mode_box[i]->setDisabled(true);
          edit_input_label[i]->setDisabled(true);
          edit_input_box[i]->setDisabled(true);
          edit_output_label[i]->setDisabled(true);
          edit_output_box[i]->setDisabled(true);
        }
        break;
  }
  edit_clock_box->setCurrentIndex(edit_card->clockSource());
  for(int i=0;i<RD_MAX_PORTS;i++) {
    edit_type_box[i]->setCurrentIndex((int)edit_card->inputPortType(i));
    if( (rdstation->cardDriver(card)==RDStation::Hpi) &&
        ((RDAudioPort::PortType)edit_type_box[i]->currentIndex()==
          RDAudioPort::Analog) ) {
      edit_input_label[i]->setEnabled(true);
      edit_input_box[i]->setEnabled(true);
    }
    else {
      edit_input_label[i]->setDisabled(true);
      edit_input_box[i]->setDisabled(true);
    }
    edit_mode_box[i]->setCurrentIndex((int)edit_card->inputPortMode(i));
    edit_input_box[i]->setValue(edit_card->inputPortLevel(i)/100);
    edit_output_box[i]->setValue(edit_card->outputPortLevel(i)/100);
  }
}


void EditAudioPorts::WriteRecord()
{
  edit_card->
    setClockSource((RDCae::ClockSource)edit_clock_box->currentIndex());
  for(int i=0;i<RD_MAX_PORTS;i++) {
    edit_card->setInputPortType(i,
		 (RDAudioPort::PortType)edit_type_box[i]->currentIndex());
    edit_card->setInputPortMode(i,
		 (RDCae::ChannelMode)edit_mode_box[i]->currentIndex());
    edit_card->setInputPortLevel(i,edit_input_box[i]->value()*100);
    edit_card->setOutputPortLevel(i,edit_output_box[i]->value()*100);
  }
}
