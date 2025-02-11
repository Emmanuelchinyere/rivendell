// driver_alsa.cpp
//
// caed(8) driver for Advanced Linux Audio Architecture devices
//
//   (C) Copyright 2021 Fred Gleason <fredg@paravelsystems.com>
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

#include <math.h>
#include <signal.h>

#include <rdconf.h>
#include <rdmeteraverage.h>
#include <rdringbuffer.h>

#include "driver_alsa.h"

#ifdef ALSA
//
// Callback Variables
//
volatile int alsa_input_channels[RD_MAX_CARDS][RD_MAX_PORTS];
volatile int alsa_output_channels[RD_MAX_CARDS][RD_MAX_STREAMS];
RDMeterAverage *alsa_input_meter[RD_MAX_CARDS][RD_MAX_PORTS][2];
RDMeterAverage *alsa_output_meter[RD_MAX_CARDS][RD_MAX_PORTS][2];
RDMeterAverage *alsa_stream_output_meter[RD_MAX_CARDS][RD_MAX_STREAMS][2];
volatile double alsa_input_volume[RD_MAX_CARDS][RD_MAX_PORTS];
volatile double alsa_output_volume[RD_MAX_CARDS][RD_MAX_PORTS][RD_MAX_STREAMS];
volatile double
  alsa_passthrough_volume[RD_MAX_CARDS][RD_MAX_PORTS][RD_MAX_PORTS];
volatile double alsa_input_vox[RD_MAX_CARDS][RD_MAX_PORTS];
RDRingBuffer *alsa_play_ring[RD_MAX_CARDS][RD_MAX_STREAMS];
RDRingBuffer *alsa_record_ring[RD_MAX_CARDS][RD_MAX_PORTS];
RDRingBuffer *alsa_passthrough_ring[RD_MAX_CARDS][RD_MAX_PORTS];
volatile bool alsa_playing[RD_MAX_CARDS][RD_MAX_STREAMS];
volatile bool alsa_stopping[RD_MAX_CARDS][RD_MAX_STREAMS];
volatile bool alsa_eof[RD_MAX_CARDS][RD_MAX_STREAMS];
volatile int alsa_output_pos[RD_MAX_CARDS][RD_MAX_STREAMS];
volatile bool alsa_recording[RD_MAX_CARDS][RD_MAX_PORTS];
volatile bool alsa_ready[RD_MAX_CARDS][RD_MAX_PORTS];

void *AlsaCaptureCallback(void *ptr)
{
  char alsa_buffer[RINGBUFFER_SIZE];
  int modulo;
  int16_t in_meter[RD_MAX_PORTS][2];
  struct alsa_format *alsa_format=(struct alsa_format *)ptr;

  signal(SIGTERM,SigHandler);
  signal(SIGINT,SigHandler);

  while(!alsa_format->exiting) {
    int s=snd_pcm_readi(alsa_format->pcm,alsa_format->card_buffer,
			rda->config()->alsaPeriodSize()/(alsa_format->periods*2));
    if(((snd_pcm_state(alsa_format->pcm)!=SND_PCM_STATE_RUNNING)&&
	(!alsa_format->exiting))||(s<0)) {
      snd_pcm_drop (alsa_format->pcm);
      snd_pcm_prepare(alsa_format->pcm);
      rda->syslog(LOG_DEBUG,"****** ALSA Capture Xrun - Card: %d ******",
		  alsa_format->card);
    }
    else {
      switch(alsa_format->format) {
      case SND_PCM_FORMAT_S16_LE:
	modulo=alsa_format->channels;
	for(unsigned i=0;i<(alsa_format->channels/2);i++) {
	  if(alsa_recording[alsa_format->card][i]) {
	    if(alsa_input_volume[alsa_format->card][i]!=0.0) {
	      switch(alsa_input_channels[alsa_format->card][i]) {
	      case 1:
		for(int k=0;k<(2*s);k++) {
		  ((int16_t *)alsa_buffer)[k]=
		    (int16_t)(alsa_input_volume[alsa_format->card][i]*
			      (double)(((int16_t *)alsa_format->
					card_buffer)
				       [modulo*k+2*i]))+
		    (int16_t)(alsa_input_volume[alsa_format->card][i]*
			      (double)(((int16_t *)alsa_format->
					card_buffer)
				       [modulo*k+2*i+1]));
		}
		alsa_record_ring[alsa_format->card][i]->
		  write(alsa_buffer,s*sizeof(int16_t));
		break;

	      case 2:
		for(int k=0;k<s;k++) {
		  ((int16_t *)alsa_buffer)[2*k]=
		    (int16_t)(alsa_input_volume[alsa_format->card][i]*
			      (double)(((int16_t *)alsa_format->
					card_buffer)
				       [modulo*k+2*i]));
		  ((int16_t *)alsa_buffer)[2*k+1]=
		    (int16_t)(alsa_input_volume[alsa_format->card][i]*
			      (double)(((int16_t *)alsa_format->
					card_buffer)
				       [modulo*k+2*i+1]));
		}
		alsa_record_ring[alsa_format->card][i]->
		  write(alsa_buffer,s*2*sizeof(int16_t));
		break;
	      }
	    }
	  }
	}

	//
	// Process Passthroughs
	//
	for(unsigned i=0;i<alsa_format->channels;i+=2) {
	  for(unsigned j=0;j<2;j++) {
	    for(int k=0;k<s;k++) {
	      ((int16_t *)alsa_format->passthrough_buffer)[2*k+j]=
		((int16_t *)alsa_format->
		 card_buffer)[alsa_format->channels*k+i+j];
	    }
	  }
	  alsa_passthrough_ring[alsa_format->card][i/2]->
	    write(alsa_format->passthrough_buffer,4*s);
	}

	//
	// Process Input Meters
	//
	for(unsigned i=0;i<alsa_format->channels;i+=2) {
	  for(unsigned j=0;j<2;j++) {
	    in_meter[i/2][j]=0;
	    for(int k=0;k<s;k++) {
	      if(((int16_t *)alsa_format->
		  card_buffer)[alsa_format->channels*k+2*i+j]>
		 in_meter[i][j]) {
		in_meter[i][j]=
		  ((int16_t *)alsa_format->
		   card_buffer)[alsa_format->channels*k+2*i+j];
	      }
	    }
	    alsa_input_meter[alsa_format->card][i/2][j]->
	      addValue(((double)in_meter[i/2][j])/32768.0);
	  }
	}
	break;

      case SND_PCM_FORMAT_S32_LE:
	modulo=alsa_format->channels*2;
	for(unsigned i=0;i<(alsa_format->channels/2);i++) {
	  if(alsa_recording[alsa_format->card][i]) {
	    if(alsa_input_volume[alsa_format->card][i]!=0.0) {
	      switch(alsa_input_channels[alsa_format->card][i]) {
	      case 1:
		for(int k=0;k<(2*s);k++) {
		  ((int16_t *)alsa_buffer)[k]=
		    (int16_t)(alsa_input_volume[alsa_format->card][i]*
			      (double)(((int16_t *)alsa_format->
					card_buffer)
				       [modulo*k+4*i+1]))+
		    (int16_t)(alsa_input_volume[alsa_format->card][i]*
			      (double)(((int16_t *)alsa_format->
					card_buffer)
				       [modulo*k+4*i+3]));
		}
		alsa_record_ring[alsa_format->card][i]->
		  write(alsa_buffer,s*sizeof(int16_t));
		break;

	      case 2:
		for(int k=0;k<s;k++) {
		  ((int16_t *)alsa_buffer)[2*k]=
		    (int16_t)(alsa_input_volume[alsa_format->card][i]*
			      (double)(((int16_t *)alsa_format->card_buffer)
				       [modulo*k+4*i+1]));
		  ((int16_t *)alsa_buffer)[2*k+1]=
		    (int16_t)(alsa_input_volume[alsa_format->card][i]*
			      (double)(((int16_t *)alsa_format->card_buffer)
				       [modulo*k+4*i+3]));
		}
		alsa_record_ring[alsa_format->card][i]->
		  write(alsa_buffer,s*2*sizeof(int16_t));
		break;
	      }
	    }
	  }
	}

	//
	// Process Passthroughs
	//
	for(unsigned i=0;i<alsa_format->channels;i+=2) {
	  for(unsigned j=0;j<2;j++) {
	    for(int k=0;k<s;k++) {
	      ((int32_t *)alsa_format->passthrough_buffer)[2*k+j]=
		((int32_t *)alsa_format->
		 card_buffer)[alsa_format->channels*k+i+j];
	    }
	  }
	  alsa_passthrough_ring[alsa_format->card][i/2]->
	    write(alsa_format->passthrough_buffer,8*s);
	}

	//
	// Process Input Meters
	//
	for(unsigned i=0;i<alsa_format->channels;i+=2) {
	  for(unsigned j=0;j<2;j++) {
	    in_meter[i/2][j]=0;
	    for(int k=0;k<s;k++) {
	      if(((int16_t *)alsa_format->
		  card_buffer)[alsa_format->channels*2*k+2*i+1+2*j]>
		 in_meter[i/2][j]) {
		in_meter[i/2][j]=
		  ((int16_t *)alsa_format->
		   card_buffer)[alsa_format->channels*2*k+2*i+1+2*j];
	      }
	    }
	    alsa_input_meter[alsa_format->card][i/2][j]->
	      addValue(((double)in_meter[i/2][j])/32768.0);
	  }
	}
	break;

      default:
	break;
      }
    }
  }

  return 0;
}


void *AlsaPlayCallback(void *ptr)
{
  int n=0;
  int p;
  char alsa_buffer[RINGBUFFER_SIZE];
  int modulo;
  int16_t out_meter[RD_MAX_PORTS][2];
  int16_t stream_out_meter=0;

  struct alsa_format *alsa_format=(struct alsa_format *)ptr;

  signal(SIGTERM,SigHandler);
  signal(SIGINT,SigHandler);

  while(!alsa_format->exiting) {
    memset(alsa_format->card_buffer,0,alsa_format->card_buffer_size);

    switch(alsa_format->format) {
    case SND_PCM_FORMAT_S16_LE:
      for(unsigned j=0;j<RD_MAX_STREAMS;j++) {
        if(alsa_playing[alsa_format->card][j]) {
          switch(alsa_output_channels[alsa_format->card][j]) {
          case 1:
            n=alsa_play_ring[alsa_format->card][j]->
              read(alsa_buffer,alsa_format->
                   buffer_size/alsa_format->periods)/
              (2*sizeof(int16_t));
            stream_out_meter=0;  // Stream Output Meters
            for(int k=0;k<n;k++) {
              if(abs(((int16_t *)alsa_buffer)[k])>stream_out_meter) {
                stream_out_meter=abs(((int16_t *)alsa_buffer)[k]);
              }
            }
            alsa_stream_output_meter[alsa_format->card][j][0]->
              addValue(((double)stream_out_meter)/32768.0);
            alsa_stream_output_meter[alsa_format->card][j][1]->
              addValue(((double)stream_out_meter)/32768.0);
            modulo=alsa_format->channels;
            for(unsigned i=0;i<(alsa_format->channels/2);i++) {
              if(alsa_output_volume[alsa_format->card][i][j]!=0.0) {
                for(int k=0;k<(2*n);k++) {
                  ((int16_t *)alsa_format->card_buffer)[modulo*k+2*i]+=
                    (int16_t)(alsa_output_volume[alsa_format->card][i][j]*
                              (double)(((int16_t *)alsa_buffer)[k]));
                  ((int16_t *)alsa_format->card_buffer)[modulo*k+2*i+1]+=
                    (int16_t)(alsa_output_volume[alsa_format->card][i][j]*
                              (double)(((int16_t *)alsa_buffer)[k]));
                }
              }
            }
            n*=2;
            break;

          case 2:
            n=alsa_play_ring[alsa_format->card][j]->
              read(alsa_buffer,alsa_format->buffer_size*2/
                   alsa_format->periods)/(2*sizeof(int16_t));
            for(unsigned k=0;k<2;k++) {  // Stream Output Meters
              stream_out_meter=0;
              for(int l=0;l<n;l+=2) {
                if(abs(((int16_t *)alsa_buffer)[l+k])>stream_out_meter) {
                  stream_out_meter=abs(((int16_t *)alsa_buffer)[l+k]);
                }
              }
              alsa_stream_output_meter[alsa_format->card][j][k]->
                addValue(((double)stream_out_meter)/32768.0);
            }
            modulo=alsa_format->channels;
            for(unsigned i=0;i<(alsa_format->channels/2);i++) {
              if(alsa_output_volume[alsa_format->card][i][j]!=0.0) {
                for(int k=0;k<n;k++) {
                  ((int16_t *)alsa_format->card_buffer)[modulo*k+2*i]+=
                    (int16_t)(alsa_output_volume[alsa_format->card][i][j]*
                              (double)(((int16_t *)alsa_buffer)[2*k]));
                  ((int16_t *)alsa_format->card_buffer)[modulo*k+2*i+1]+=
                    (int16_t)(alsa_output_volume[alsa_format->card][i][j]*
                              (double)(((int16_t *)alsa_buffer)[2*k+1]));
                }
              }
            }
            break;
          }
          alsa_output_pos[alsa_format->card][j]+=n;
          if((n==0)&&alsa_eof[alsa_format->card][j]) {
            alsa_stopping[alsa_format->card][j]=true;
          }
        }
      }
      n=alsa_format->buffer_size/(2*alsa_format->periods);

      //
      // Process Passthroughs
      //
      for(unsigned i=0;i<alsa_format->capture_channels;i+=2) {
        p=alsa_passthrough_ring[alsa_format->card][i/2]->
          read(alsa_format->passthrough_buffer,4*n)/4;
        bool zero_volume = true;
        for (unsigned j=0;j<alsa_format->channels && zero_volume;j+=1) {
          zero_volume = (alsa_passthrough_volume[alsa_format->card][i/2][j] == 0.0);
        }
        if (!zero_volume) {
          for(unsigned j=0;j<alsa_format->channels;j+=2) {
            double passthrough_volume = alsa_passthrough_volume[alsa_format->card][i/2][j/2];
            if (passthrough_volume != 0.0) {
              for(unsigned k=0;k<2;k++) {
                for(int l=0;l<p;l++) {
                  ((int16_t *)alsa_format->
                   card_buffer)[alsa_format->channels*l+j+k]+=
                    (int16_t)((double)((int16_t *)alsa_format->passthrough_buffer)[2*l+k]*passthrough_volume);
                }
              }
            }
          }
        }
      }

      //
      // Process Output Meters
      //
      for(unsigned i=0;i<alsa_format->channels;i+=2) {
        unsigned port=i/2;
        for(unsigned j=0;j<2;j++) {
          out_meter[port][j]=0;
          for(unsigned k=0;k<alsa_format->buffer_size;k++) {
            int16_t sample = ((int16_t *)alsa_format->
                              card_buffer)[alsa_format->channels*k+2*i+j];

            if(sample> out_meter[i][j]) {
              out_meter[i][j]= sample;
            }
          }
          alsa_output_meter[alsa_format->card][i][j]->
            addValue(((double)out_meter[i][j])/32768.0);
        }
      }
      break;

    case SND_PCM_FORMAT_S32_LE:
      for(unsigned j=0;j<RD_MAX_STREAMS;j++) {
        if(alsa_playing[alsa_format->card][j]) {
          switch(alsa_output_channels[alsa_format->card][j]) {
          case 1:
            n=alsa_play_ring[alsa_format->card][j]->
              read(alsa_buffer,alsa_format->buffer_size/
                   alsa_format->periods)/(2*sizeof(int16_t));
            stream_out_meter=0;
            for(int k=0;k<n;k++) {  // Stream Output Meters
              if(abs(((int16_t *)alsa_buffer)[k])>stream_out_meter) {
                stream_out_meter=abs(((int16_t *)alsa_buffer)[k]);
              }
            }
            alsa_stream_output_meter[alsa_format->card][j][0]->
              addValue(((double)stream_out_meter)/32768.0);
            alsa_stream_output_meter[alsa_format->card][j][1]->
              addValue(((double)stream_out_meter)/32768.0);
            modulo=alsa_format->channels*2;
            for(unsigned i=0;i<(alsa_format->channels/2);i++) {
              if(alsa_output_volume[alsa_format->card][i][j]!=0.0) {
                for(int k=0;k<(2*n);k++) {
                  ((int16_t *)alsa_format->card_buffer)[modulo*k+4*i+1]+=
                    (int16_t)(alsa_output_volume[alsa_format->card][i][j]*
                              (double)(((int16_t *)alsa_buffer)[k]));
                  ((int16_t *)alsa_format->card_buffer)[modulo*k+4*i+3]+=
                    (int16_t)(alsa_output_volume[alsa_format->card][i][j]*
                              (double)(((int16_t *)alsa_buffer)[k]));
                }
              }
            }
            n*=2;
            break;

          case 2:
            n=alsa_play_ring[alsa_format->card][j]->
              read(alsa_buffer,alsa_format->buffer_size*2/
                   alsa_format->periods)/(2*sizeof(int16_t));
            for(unsigned k=0;k<2;k++) {  // Stream Output Meters
              stream_out_meter=0;
              for(int l=0;l<n;l+=2) {
                if(abs(((int16_t *)alsa_buffer)[l+k])>stream_out_meter) {
                  stream_out_meter=abs(((int16_t *)alsa_buffer)[l+k]);
                }
              }
              alsa_stream_output_meter[alsa_format->card][j][k]->
                addValue(((double)stream_out_meter)/32768.0);
            }
            modulo=alsa_format->channels*2;
            for(unsigned i=0;i<(alsa_format->channels/2);i++) {
              if(alsa_output_volume[alsa_format->card][i][j]!=0.0) {
                for(int k=0;k<n;k++) {
                  ((int16_t *)alsa_format->card_buffer)[modulo*k+4*i+1]+=
                    (int16_t)(alsa_output_volume[alsa_format->card][i][j]*
                              (double)(((int16_t *)alsa_buffer)[2*k]));
                  ((int16_t *)alsa_format->card_buffer)[modulo*k+4*i+3]+=
                    (int16_t)(alsa_output_volume[alsa_format->card][i][j]*
                              (double)(((int16_t *)alsa_buffer)[2*k+1]));
                }
              }
            }
            break;
          }
          alsa_output_pos[alsa_format->card][j]+=n;
          if((n==0)&&alsa_eof[alsa_format->card][j]) {
            alsa_stopping[alsa_format->card][j]=true;
            // Empty the ring buffer
            while(alsa_play_ring[alsa_format->card][j]->
                  read(alsa_buffer,alsa_format->buffer_size*2/
                       alsa_format->periods)/(2*sizeof(int16_t))>0);
          }
        }
      }
      n=alsa_format->buffer_size/(2*alsa_format->periods);

      //
      // Process Passthroughs
      //
      for(unsigned i=0;i<alsa_format->capture_channels;i+=2) {
        p=alsa_passthrough_ring[alsa_format->card][i/2]->
          read(alsa_format->passthrough_buffer,8*n)/8;
        bool zero_volume = true;
        for (unsigned j=0;j<alsa_format->channels && zero_volume;j+=1) {
          zero_volume = (alsa_passthrough_volume[alsa_format->card][i/2][j] == 0.0);
        }
        if (!zero_volume) {
          for(unsigned j=0;j<alsa_format->channels;j+=2) {
            double passthrough_volume = alsa_passthrough_volume[alsa_format->card][i/2][j/2];
            if (passthrough_volume != 0.0) {
              for(unsigned k=0;k<2;k++) {
                for(int l=0;l<p;l++) {
                  ((int32_t *)alsa_format->
                   card_buffer)[alsa_format->channels*l+j+k]+=
                    (int32_t)((double)((int32_t *)alsa_format->passthrough_buffer)[2*l+k]*passthrough_volume);
                }
              }
            }
          }
        }
      }

      //
      // Process Output Meters
      //
      unsigned buffer_width;
      buffer_width = (alsa_format->buffer_size*2/alsa_format->periods);
      for(unsigned i=0;i<alsa_format->channels;i+=2) {
        unsigned port=i/2;
        for(unsigned j=0;j<2;j++) {
          out_meter[port][j]=0;
          for(unsigned k=0; k<buffer_width; k++) {
            int16_t sample = ((int16_t *)alsa_format->
                              card_buffer)[alsa_format->channels*2*k+2*i+1+2*j];
            if (sample > out_meter[port][j]) {
              out_meter[port][j] = sample;
            }
          }
          alsa_output_meter[alsa_format->card][port][j]->
            addValue(((double)out_meter[port][j])/32768.0);
        }
      }
      break;

    default:
      break;
    }
    int s=snd_pcm_writei(alsa_format->pcm,alsa_format->card_buffer,n);
    if(s!=n) {
      if(s<0) {
	rda->syslog(LOG_WARNING,
			      "*** alsa error %d: %s",-s,snd_strerror(s));
      }
      else {
        rda->syslog(LOG_WARNING,
			      "period size mismatch - wrote %d",s);
      }
    }
    if((snd_pcm_state(alsa_format->pcm)!=SND_PCM_STATE_RUNNING)&&
       (!alsa_format->exiting)) {
      snd_pcm_drop (alsa_format->pcm);
      snd_pcm_prepare(alsa_format->pcm);
      rda->syslog(LOG_DEBUG,
			    "****** ALSA Playout Xrun - Card: %d ******",
	     alsa_format->card);
    }
  }

  return 0;
}


void DriverAlsa::AlsaInitCallback()
{
  int avg_periods=
    (330*systemSampleRate())/(1000*rda->config()->alsaPeriodSize());
  for(int i=0;i<RD_MAX_CARDS;i++) {
    for(int j=0;j<RD_MAX_PORTS;j++) {
      alsa_recording[i][j]=false;
      alsa_ready[i][j]=false;
      alsa_input_volume[i][j]=1.0;
      alsa_input_vox[i][j]=0.0;
      for(int k=0;k<2;k++) {
	alsa_input_meter[i][j][k]=new RDMeterAverage(avg_periods);
	alsa_output_meter[i][j][k]=new RDMeterAverage(avg_periods);
      }
      for(int k=0;k<RD_MAX_STREAMS;k++) {
	alsa_output_volume[i][j][k]=1.0;
      }
      alsa_passthrough_ring[i][j]=new RDRingBuffer(RINGBUFFER_SIZE);
      alsa_passthrough_ring[i][j]->reset();
      alsa_record_ring[i][j]=NULL;
      for(int k=0;k<RD_MAX_PORTS;k++) {
	alsa_passthrough_volume[i][j][k]=0.0;
      }
    }
    for(int j=0;j<RD_MAX_STREAMS;j++) {
      alsa_play_ring[i][j]=NULL;
      alsa_playing[i][j]=false;
      for(int k=0;k<2;k++) {
	alsa_stream_output_meter[i][j][k]=new RDMeterAverage(avg_periods);
      }
    }
  }
}
#endif  // ALSA


DriverAlsa::DriverAlsa(QObject *parent)
  : Driver(RDStation::Alsa,parent)
{
#ifdef ALSA
  //
  // Initialize Data Structures
  //
  for(int i=0;i<RD_MAX_CARDS;i++) {
    for(int j=0;j<RD_MAX_STREAMS;j++) {
      alsa_input_volume_db[i][j]=0;
      alsa_samples_recorded[i][j]=0;
#ifdef HAVE_MAD
      mad_mpeg[i][j]=new unsigned char[16384];
#endif  // HAVE_MAD
      for(int k=0;k<RD_MAX_PORTS;k++) {
	alsa_output_volume_db[i][k][j]=0;
      }
    }
    for(int j=0;j<RD_MAX_PORTS;j++) {
      for(int k=0;k<RD_MAX_PORTS;k++) {
	alsa_passthrough_volume_db[i][j][k]=RD_MUTE_DEPTH;
      }
    }
  }

  //
  // Stop & Fade Timers
  //
  QSignalMapper *stop_mapper=new QSignalMapper(this);
  connect(stop_mapper,SIGNAL(mapped(int)),this,SLOT(stopTimerData(int)));
  QSignalMapper *fade_mapper=new QSignalMapper(this);
  connect(fade_mapper,SIGNAL(mapped(int)),this,SLOT(fadeTimerData(int)));
  QSignalMapper *record_mapper=new QSignalMapper(this);
  connect(record_mapper,SIGNAL(mapped(int)),this,SLOT(recordTimerData(int)));
  for(int i=0;i<RD_MAX_CARDS;i++) {
    for(int j=0;j<RD_MAX_STREAMS;j++) {
      alsa_stop_timer[i][j]=new QTimer(this);
      alsa_stop_timer[i][j]->setSingleShot(true);
      stop_mapper->setMapping(alsa_stop_timer[i][j],i*RD_MAX_STREAMS+j);
      connect(alsa_stop_timer[i][j],SIGNAL(timeout()),stop_mapper,SLOT(map()));
      alsa_fade_timer[i][j]=new QTimer(this);
      fade_mapper->setMapping(alsa_fade_timer[i][j],i*RD_MAX_STREAMS+j);
      connect(alsa_fade_timer[i][j],SIGNAL(timeout()),fade_mapper,SLOT(map()));
    }
    for(int j=0;j<RD_MAX_PORTS;j++) {
      alsa_record_timer[i][j]=new QTimer(this);
      alsa_record_timer[i][j]->setSingleShot(true);
      record_mapper->setMapping(alsa_record_timer[i][j],i*RD_MAX_PORTS+j);
      connect(alsa_record_timer[i][j],SIGNAL(timeout()),
	      record_mapper,SLOT(map()));
    }
  }

  //
  // Allocate Temporary Buffers
  //
  AlsaInitCallback();
  alsa_wave_buffer=new int16_t[RINGBUFFER_SIZE];
  alsa_wave24_buffer=new uint8_t[2*RINGBUFFER_SIZE];

  LoadTwoLame();
  LoadMad();
#endif  // ALSA
}


DriverAlsa::~DriverAlsa()
{
#ifdef ALSA
  for(int i=0;i<RD_MAX_CARDS;i++) {
    if(hasCard(i)) {
      alsa_play_format[i].exiting=true;
      pthread_join(alsa_play_format[i].thread,NULL);
      snd_pcm_close(alsa_play_format[i].pcm);
      if(alsa_capture_format[i].pcm!=NULL) {
	alsa_capture_format[i].exiting=true;
	pthread_join(alsa_capture_format[i].thread,NULL);
	snd_pcm_close(alsa_capture_format[i].pcm);
      }
    }
  }
#endif  // ALSA
}


QString DriverAlsa::version() const
{
#ifdef ALSA
  return 
    QString::asprintf("%d.%d.%d",SND_LIB_MAJOR,SND_LIB_MINOR,SND_LIB_SUBMINOR);
#else
  return QString();
#endif  // ALSA
}


bool DriverAlsa::initialize(unsigned *next_cardnum)
{
#ifdef ALSA
  QString dev;
  snd_pcm_t *pcm_play_handle;
  snd_pcm_t *pcm_capture_handle;
  snd_ctl_t *snd_ctl;
  snd_ctl_card_info_t *card_info=NULL;
  bool pcm_opened=false;
  int card=0;

  //
  // Start Up Interfaces
  //
  while((*next_cardnum)<RD_MAX_CARDS) {
    pcm_opened=false;
    alsa_play_format[*next_cardnum].exiting = true;
    alsa_capture_format[*next_cardnum].exiting = true;
    dev=QString::asprintf("rd%d",card);
    if(snd_pcm_open(&pcm_play_handle,dev.toUtf8(),
		    SND_PCM_STREAM_PLAYBACK,0)==0){
      pcm_opened=true;
      if(!AlsaStartPlayDevice(dev,*next_cardnum,pcm_play_handle)) {
	snd_pcm_close(pcm_play_handle);
      }
    }
    if(snd_pcm_open(&pcm_capture_handle,dev.toUtf8(),
		    SND_PCM_STREAM_CAPTURE,0)==0) {
      pcm_opened=true;
      if(!AlsaStartCaptureDevice(dev,*next_cardnum,pcm_capture_handle)) {
	snd_pcm_close(pcm_capture_handle);
      }
    }
    if(!pcm_opened) {
      return card>0;
    }
    rda->station()->setCardDriver(*next_cardnum,RDStation::Alsa);
    if(snd_ctl_open(&snd_ctl,dev.toUtf8(),0)<0) {
      rda->syslog(LOG_INFO,
		  "no control device found for %s",
		  dev.toUtf8().constData());
      rda->station()->setCardName(*next_cardnum,tr("ALSA Device")+" "+dev);
    }
    else {
      snd_ctl_card_info_malloc(&card_info);
      snd_ctl_card_info(snd_ctl,card_info);
      rda->station()->
	setCardName(*next_cardnum,snd_ctl_card_info_get_longname(card_info));
      snd_ctl_close(snd_ctl);
    }
    alsa_input_port_quantities[*next_cardnum]=
      alsa_capture_format[*next_cardnum].channels/RD_DEFAULT_CHANNELS;
    rda->station()->setCardInputs(*next_cardnum,
			    alsa_input_port_quantities.value(*next_cardnum));
		    
    alsa_output_port_quantities[*next_cardnum]=
      alsa_play_format[*next_cardnum].channels/RD_DEFAULT_CHANNELS;
    rda->station()->setCardOutputs(*next_cardnum,
			    alsa_output_port_quantities.value(*next_cardnum));
		     
    card++;
    if(!pcm_opened) {
      return card>0;
    }
    addCard(*next_cardnum);
    (*next_cardnum)++;
  }
  return card>0;
#else
  return false;
#endif  // ALSA
}


int DriverAlsa::inputPortQuantity(int card) const
{
  return alsa_input_port_quantities.value(card);
}


int DriverAlsa::outputPortQuantity(int card) const
{
  return alsa_output_port_quantities.value(card);
}


bool DriverAlsa::loadPlayback(int card,QString wavename,int *stream)
{
#ifdef ALSA
  if(alsa_play_format[card].exiting||((*stream=GetAlsaOutputStream(card))<0)) {
    rda->syslog(LOG_DEBUG,"alsaLoadPlayback(%s) GetAlsaOutputStream():%d < 0",
		wavename.toUtf8().constData(),*stream);
    return false;
  }
  alsa_play_wave[card][*stream]=new RDWaveFile(wavename);
  if(!alsa_play_wave[card][*stream]->openWave()) {
    rda->syslog(LOG_DEBUG,"alsaLoadPlayback(%s) openWave() failed to open file",
		wavename.toUtf8().constData());
    delete alsa_play_wave[card][*stream];
    alsa_play_wave[card][*stream]=NULL;
    FreeAlsaOutputStream(card,*stream);
    *stream=-1;
    return false;
  }
  switch(alsa_play_wave[card][*stream]->getFormatTag()) {
  case WAVE_FORMAT_PCM:
  case WAVE_FORMAT_VORBIS:
    break;

  case WAVE_FORMAT_MPEG:
    if(!InitMadDecoder(card,*stream,alsa_play_wave[card][*stream])) {
      delete alsa_play_wave[card][*stream];
      alsa_play_wave[card][*stream]=NULL;
      FreeAlsaOutputStream(card,*stream);
      *stream=-1;
      return false;
    }
    break;

  default:
    rda->syslog(LOG_WARNING,
	"alsaLoadPlayback(%s) getFormatTag()%d || getBistsPerSample()%d failed",
		wavename.toUtf8().constData(),
	   alsa_play_wave[card][*stream]->getFormatTag(),
	   alsa_play_wave[card][*stream]->getBitsPerSample());
    delete alsa_play_wave[card][*stream];
    alsa_play_wave[card][*stream]=NULL;
    FreeAlsaOutputStream(card,*stream);
    *stream=-1;
    return false;
  }
  alsa_output_channels[card][*stream]=
    alsa_play_wave[card][*stream]->getChannels();
  alsa_stopping[card][*stream]=false;
  alsa_offset[card][*stream]=0;
  alsa_output_pos[card][*stream]=0;
  alsa_eof[card][*stream]=false;
  alsa_play_ring[card][*stream]->reset();
  FillAlsaOutputStream(card,*stream);
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::unloadPlayback(int card,int stream)
{
#ifdef ALSA
  if(alsa_play_ring[card][stream]==NULL) {
    return false;
  }
  alsa_playing[card][stream]=false;
  switch(alsa_play_wave[card][stream]->getFormatTag()) {
  case WAVE_FORMAT_MPEG:
    FreeMadDecoder(card,stream);
    break;
  }
  alsa_play_wave[card][stream]->closeWave();
  delete alsa_play_wave[card][stream];
  alsa_play_wave[card][stream]=NULL;
  FreeAlsaOutputStream(card,stream);
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::playbackPosition(int card,int stream,unsigned pos)
{
#ifdef ALSA
  unsigned offset=0;

  if(alsa_play_format[card].exiting){
    return false;
  }
  switch(alsa_play_wave[card][stream]->getFormatTag()) {
  case WAVE_FORMAT_PCM:
    offset=(unsigned)((double)alsa_play_wave[card][stream]->getSamplesPerSec()*
		      (double)alsa_play_wave[card][stream]->getBlockAlign()*
		      (double)pos/1000);
    alsa_offset[card][stream]=
      offset/alsa_play_wave[card][stream]->getBlockAlign();
    offset=
      alsa_offset[card][stream]*alsa_play_wave[card][stream]->getBlockAlign();
    break;

  case WAVE_FORMAT_MPEG:
    offset=(unsigned)((double)alsa_play_wave[card][stream]->getSamplesPerSec()*
		      (double)pos/1000);
    alsa_offset[card][stream]=offset/1152*1152;
    offset=alsa_offset[card][stream]/1152*
      alsa_play_wave[card][stream]->getBlockAlign();
    FreeMadDecoder(card,stream);
    InitMadDecoder(card,stream,alsa_play_wave[card][stream]);
    break;
  }
  if(alsa_offset[card][stream]>
     (int)alsa_play_wave[card][stream]->getSampleLength()) {
    return false;
  }
  alsa_output_pos[card][stream]=0;
  alsa_play_wave[card][stream]->seekWave(offset,SEEK_SET);
  alsa_eof[card][stream]=false;
  alsa_play_ring[card][stream]->reset();
  FillAlsaOutputStream(card,stream);

  if(alsa_playing[card][stream]) {
    alsa_stop_timer[card][stream]->stop();
    alsa_stop_timer[card][stream]->
      start(alsa_play_wave[card][stream]->getExtTimeLength()-pos);
  }
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::play(int card,int stream,int length,int speed,bool pitch,
		      bool rates)
{
#ifdef ALSA
  if((alsa_play_ring[card][stream]==NULL)||
     alsa_playing[card][stream]||(speed!=RD_TIMESCALE_DIVISOR)) {
    return false;
  }
  alsa_playing[card][stream]=true;
  if(length>0) {
    alsa_stop_timer[card][stream]->start(length);
  }
  statePlayUpdate(card,stream,1);
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::stopPlayback(int card,int stream)
{
#ifdef ALSA
  if((alsa_play_ring[card][stream]==NULL)||(!alsa_playing[card][stream])) {
    return false;
  }
  alsa_playing[card][stream]=false;
  alsa_play_ring[card][stream]->reset();
  alsa_stop_timer[card][stream]->stop();
  statePlayUpdate(card,stream,2);
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::timescaleSupported(int card)
{
  return false;
}


bool DriverAlsa::loadRecord(int card,int port,int coding,int chans,int samprate,
			    int bitrate,QString wavename)
{
#ifdef ALSA
  alsa_record_wave[card][port]=new RDWaveFile(wavename);
  switch(coding) {
  case 0:  // PCM16
    alsa_record_wave[card][port]->setFormatTag(WAVE_FORMAT_PCM);
    alsa_record_wave[card][port]->setChannels(chans);
    alsa_record_wave[card][port]->setSamplesPerSec(samprate);
    alsa_record_wave[card][port]->setBitsPerSample(16);
    break;

  case 4:  // PCM24
    alsa_record_wave[card][port]->setFormatTag(WAVE_FORMAT_PCM);
    alsa_record_wave[card][port]->setChannels(chans);
    alsa_record_wave[card][port]->setSamplesPerSec(samprate);
    alsa_record_wave[card][port]->setBitsPerSample(24);
    break;

  case 2:  // MPEG Layer 2
    if(!InitTwoLameEncoder(card,port,chans,samprate,bitrate)) {
      delete alsa_record_wave[card][port];
      alsa_record_wave[card][port]=NULL;
      return false;
    }
    alsa_record_wave[card][port]->setFormatTag(WAVE_FORMAT_MPEG);
    alsa_record_wave[card][port]->setChannels(chans);
    alsa_record_wave[card][port]->setSamplesPerSec(samprate);
    alsa_record_wave[card][port]->setBitsPerSample(16);
    alsa_record_wave[card][port]->setHeadLayer(ACM_MPEG_LAYER2);
    switch(chans) {
    case 1:
      alsa_record_wave[card][port]->setHeadMode(ACM_MPEG_SINGLECHANNEL);
      break;

    case 2:
      alsa_record_wave[card][port]->setHeadMode(ACM_MPEG_STEREO);
      break;

    default:
      rda->syslog(LOG_WARNING,
	     "requested unsupported channel count %d, card: %d, port: %d",
	     chans,card,port);
      delete alsa_record_wave[card][port];
      alsa_record_wave[card][port]=NULL;
      return false;
    }
    alsa_record_wave[card][port]->setHeadBitRate(bitrate);
    alsa_record_wave[card][port]->setMextChunk(true);
    alsa_record_wave[card][port]->setMextHomogenous(true);
    alsa_record_wave[card][port]->setMextPaddingUsed(false);
    alsa_record_wave[card][port]->setMextHackedBitRate(true);
    alsa_record_wave[card][port]->setMextFreeFormat(false);
    alsa_record_wave[card][port]->
      setMextFrameSize(144*alsa_record_wave[card][port]->getHeadBitRate()/
		       alsa_record_wave[card][port]->getSamplesPerSec());
    alsa_record_wave[card][port]->setMextAncillaryLength(5);
    alsa_record_wave[card][port]->setMextLeftEnergyPresent(true);
    if(chans>1) {
      alsa_record_wave[card][port]->setMextRightEnergyPresent(true);
    }
    else {
      alsa_record_wave[card][port]->setMextRightEnergyPresent(false);
    }
    alsa_record_wave[card][port]->setMextPrivateDataPresent(false);
    break;

  default:
    rda->syslog(LOG_WARNING,
	   "requested invalid audio encoding %d, card: %d, port: %d",
	   coding,card,port);
    delete alsa_record_wave[card][port];
    alsa_record_wave[card][port]=NULL;
    return false;
  }
  alsa_record_wave[card][port]->setBextChunk(true);
  alsa_record_wave[card][port]->setLevlChunk(true);
  if(!alsa_record_wave[card][port]->createWave()) {
    delete alsa_record_wave[card][port];
    alsa_record_wave[card][port]=NULL;
    return false;
  }
  RDCheckExitCode(rda->config(),"alsaLoadRecord() chown",
		  chown(wavename.toUtf8(),rda->config()->uid(),rda->config()->gid()));
  alsa_input_channels[card][port]=chans;
  alsa_record_ring[card][port]=new RDRingBuffer(RINGBUFFER_SIZE);
  alsa_record_ring[card][port]->reset();
  alsa_ready[card][port]=true;
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::unloadRecord(int card,int port,unsigned *len)
{
#ifdef ALSA
  alsa_recording[card][port]=false;
  alsa_ready[card][port]=false;
  EmptyAlsaInputStream(card,port);
  *len=alsa_samples_recorded[card][port];
  alsa_samples_recorded[card][port]=0;
  alsa_record_wave[card][port]->closeWave(*len);
  delete alsa_record_wave[card][port];
  alsa_record_wave[card][port]=NULL;
  delete alsa_record_ring[card][port];
  alsa_record_ring[card][port]=NULL;
  FreeTwoLameEncoder(card,port);
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::record(int card,int port,int length,int thres)
{
#ifdef ALSA
  if(!alsa_ready[card][port]) {
    return false;
  }
  alsa_recording[card][port]=true;
  if(alsa_input_vox[card][port]==0.0) {
    if(length>0) {
      alsa_record_timer[card][port]->start(length);
    }
    stateRecordUpdate(card,port,4);
  }
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::stopRecord(int card,int port)
{
#ifdef ALSA
  if(!alsa_recording[card][port]) {
    return false;
  }
  alsa_recording[card][port]=false;
  stateRecordUpdate(card,port,2);
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::setClockSource(int card,int src)
{
#ifdef ALSA
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::setInputVolume(int card,int stream,int level)
{
#ifdef ALSA
  if(level>-10000) {
    alsa_input_volume[card][stream]=pow(10.0,(double)level/2000.0);
    alsa_input_volume_db[card][stream]=level;
  }
  else {
    alsa_input_volume[card][stream]=0.0;
    alsa_input_volume_db[card][stream]=-10000;
  }
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::setOutputVolume(int card,int stream,int port,int level)
{
#ifdef ALSA
  if(level>-10000) {
    alsa_output_volume[card][port][stream]=pow(10.0,(double)level/2000.0);
    alsa_output_volume_db[card][port][stream]=level;
  }
  else {
    alsa_output_volume[card][port][stream]=0.0;
    alsa_output_volume_db[card][port][stream]=-10000;
  }
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::fadeOutputVolume(int card,int stream,int port,int level,
				  int length)
{
#ifdef ALSA
  int diff;

  if(alsa_fade_timer[card][stream]->isActive()) {
    alsa_fade_timer[card][stream]->stop();
  }
  if(level>alsa_output_volume_db[card][port][stream]) {
    alsa_fade_up[card][stream]=true;
    diff=level-alsa_output_volume_db[card][port][stream];
  }
  else {
    alsa_fade_up[card][stream]=false;
    diff=alsa_output_volume_db[card][port][stream]-level;
  }
  alsa_fade_volume_db[card][stream]=level;
  alsa_fade_port[card][stream]=port;
  alsa_fade_increment[card][stream]=diff*RD_ALSA_FADE_INTERVAL/length;
  alsa_fade_timer[card][stream]->start(RD_ALSA_FADE_INTERVAL);
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::setInputLevel(int card,int port,int level)
{
#ifdef ALSA
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::setOutputLevel(int card,int port,int level)
{
#ifdef ALSA
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::setInputMode(int card,int stream,int mode)
{
#ifdef ALSA
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::setOutputMode(int card,int stream,int mode)
{
#ifdef ALSA
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::setInputVoxLevel(int card,int stream,int level)
{
#ifdef ALSA
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::setInputType(int card,int port,int type)
{
#ifdef ALSA
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::getInputStatus(int card,int port)
{
#ifdef ALSA
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::getInputMeters(int card,int port,short levels[2])
{
#ifdef ALSA
  double meter;

  for(int i=0;i<2;i++) {

    meter=alsa_input_meter[card][port][i]->average();
    if(meter==0.0) {
      levels[i]=-10000;
    }
    else {
      levels[i]=(int16_t)(2000.0*log10(meter));
      if(levels[i]<-10000) {
	levels[i]=-10000;
      }
    }
  }
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::getOutputMeters(int card,int port,short levels[2])
{
#ifdef ALSA
  double meter;

  for(int i=0;i<2;i++) {
    meter=alsa_output_meter[card][port][i]->average();
    if(meter==0.0) {
      levels[i]=-10000;
    }
    else {
      levels[i]=(int16_t)(2000.0*log10(meter));
      if(levels[i]<-10000) {
	levels[i]=-10000;
      }
    }
  }
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::getStreamOutputMeters(int card,int stream,short levels[2])
{
#ifdef ALSA
  double meter;

  for(int i=0;i<2;i++) {
    meter=alsa_stream_output_meter[card][stream][i]->average();
    if(meter==0.0) {
      levels[i]=-10000;
    }
    else {
      levels[i]=(int16_t)(2000.0*log10(meter));
      if(levels[i]<-10000) {
	levels[i]=-10000;
      }
    }
  }
  return true;
#else
  return false;
#endif  // ALSA
}


bool DriverAlsa::setPassthroughLevel(int card,int in_port,int out_port,
				     int level)
{
#ifdef ALSA
  if(level>-10000) {
    alsa_passthrough_volume[card][in_port][out_port]=
      pow(10.0,(double)level/2000.0);
    alsa_passthrough_volume_db[card][in_port][out_port]=level;
  }
  else {
    alsa_passthrough_volume[card][in_port][out_port]=0.0;
    alsa_passthrough_volume_db[card][in_port][out_port]=-10000;
  }
  return true;
#else
  return false;
#endif  // ALSA
}


void DriverAlsa::getOutputPosition(int card,unsigned *pos)
{// pos is in miliseconds
#ifdef ALSA
  for(int i=0;i<RD_MAX_STREAMS;i++) {
    if((!alsa_play_format[card].exiting)&&(alsa_play_wave[card][i]!=NULL)) {
      pos[i]=1000*(unsigned long long)(alsa_offset[card][i]+
				       alsa_output_pos[card][i])/
	alsa_play_wave[card][i]->getSamplesPerSec();
    }
    else {
      pos[i]=0;
    }
  }
#endif  // ALSA
}


void DriverAlsa::processBuffers()
{
#ifdef ALSA
  for(int i=0;i<RD_MAX_CARDS;i++) {
    if(hasCard(i)) {
      for(int j=0;j<RD_MAX_STREAMS;j++) {
	if(alsa_stopping[i][j]) {
	  alsa_stopping[i][j]=false;
	  alsa_eof[i][j]=false;
	  alsa_playing[i][j]=false;
	  statePlayUpdate(i,j,2);
	}
	if(alsa_playing[i][j]) {
	  FillAlsaOutputStream(i,j);
	}
      }
      for(int j=0;j<RD_MAX_PORTS;j++) {
	if(alsa_recording[i][j]) {
	  EmptyAlsaInputStream(i,j);
	}
      }
    }
  }
#endif  // ALSA
}


void DriverAlsa::stopTimerData(int cardstream)
{
#ifdef ALSA
  int card=cardstream/RD_MAX_STREAMS;
  int stream=cardstream-card*RD_MAX_STREAMS;

  stopPlayback(card,stream);
#endif  // ALSA
}


void DriverAlsa::fadeTimerData(int cardstream)
{
#ifdef ALSA
  int card=cardstream/RD_MAX_STREAMS;
  int stream=cardstream-card*RD_MAX_STREAMS;
  int16_t level;
  if(alsa_fade_up[card][stream]) {
    level=alsa_output_volume_db[card][alsa_fade_port[card][stream]][stream]+
      alsa_fade_increment[card][stream];
    if(level>=alsa_fade_volume_db[card][stream]) {
      level=alsa_fade_volume_db[card][stream];
      alsa_fade_timer[card][stream]->stop();
    }
  }
  else {
    level=alsa_output_volume_db[card][alsa_fade_port[card][stream]][stream]-
      alsa_fade_increment[card][stream];
    if(level<=alsa_fade_volume_db[card][stream]) {
      level=alsa_fade_volume_db[card][stream];
      alsa_fade_timer[card][stream]->stop();
    }
  }
  rda->syslog(LOG_DEBUG,"FadeLevel: %d",level);
  setOutputVolume(card,stream,alsa_fade_port[card][stream],level);
#endif  // ALSA
}


void DriverAlsa::recordTimerData(int cardport)
{
#ifdef ALSA
  int card=cardport/RD_MAX_PORTS;
  int stream=cardport-card*RD_MAX_PORTS;

  stopRecord(card,stream);
#endif  // ALSA
}


#ifdef ALSA
bool DriverAlsa::AlsaStartCaptureDevice(QString &dev,int card,snd_pcm_t *pcm)
{
  snd_pcm_hw_params_t *hwparams;
  snd_pcm_sw_params_t *swparams;
  int dir;
  int err;
  pthread_attr_t pthread_attr;
  unsigned sr;

  memset(&alsa_capture_format[card],0,sizeof(struct alsa_format));

  snd_pcm_hw_params_alloca(&hwparams);
  snd_pcm_hw_params_any(pcm,hwparams);

  rda->syslog(LOG_INFO,"Starting ALSA Capture Device %s:",
	 (const char *)dev.toUtf8());

  //
  // Access Type
  //
  if(snd_pcm_hw_params_test_access(pcm,hwparams,
				   SND_PCM_ACCESS_RW_INTERLEAVED)<0) {
    rda->syslog(LOG_WARNING,
			  "  Interleaved access not supported,");
    rda->syslog(LOG_WARNING,
			  "  aborting initialization of device.");
    return false;
  }
  snd_pcm_hw_params_set_access(pcm,hwparams,SND_PCM_ACCESS_RW_INTERLEAVED);

  //
  // Sample Format
  //
  if(snd_pcm_hw_params_test_format(pcm,hwparams,SND_PCM_FORMAT_S32_LE)==0) {
    alsa_capture_format[card].format=SND_PCM_FORMAT_S32_LE;
    rda->syslog(LOG_INFO,"  Format = 32 bit little-endian");
  }
  else {
    if(snd_pcm_hw_params_test_format(pcm,hwparams,SND_PCM_FORMAT_S16_LE)==0) {
      alsa_capture_format[card].format=SND_PCM_FORMAT_S16_LE;
      rda->syslog(LOG_INFO,
			    "  Format = 16 bit little-endian");
    }
    else {
      rda->syslog(LOG_WARNING,
	     "  Neither 16 nor 32 bit little-endian formats available,");
      rda->syslog(LOG_WARNING,
			    "  aborting initialization of device.");
      return false;
    }
  }
  snd_pcm_hw_params_set_format(pcm,hwparams,alsa_capture_format[card].format);

  //
  // Sample Rate
  //
  if(alsa_play_format[card].sample_rate>0) {
    sr=alsa_play_format[card].sample_rate;
  }
  else {
    sr=systemSampleRate();
  }
  snd_pcm_hw_params_set_rate_near(pcm,hwparams,&sr,&dir);
  if((sr<(systemSampleRate()-RD_ALSA_SAMPLE_RATE_TOLERANCE))||
     (sr>(systemSampleRate()+RD_ALSA_SAMPLE_RATE_TOLERANCE))) {
    rda->syslog(LOG_WARNING,
			  "  Asked for sample rate %u, got %u",
	   systemSampleRate(),sr);
    rda->syslog(LOG_WARNING,
			  "  Sample rate unsupported by device");
    return false;
  }
  alsa_capture_format[card].sample_rate=sr;
  rda->syslog(LOG_INFO,"  SampleRate = %u",sr);

  //
  // Channels
  //
  if(rda->config()->alsaChannelsPerPcm()<0) {
    alsa_capture_format[card].channels=RD_DEFAULT_CHANNELS*RD_MAX_PORTS;
  }
  else {
    alsa_capture_format[card].channels=rda->config()->alsaChannelsPerPcm();
  }
  snd_pcm_hw_params_set_channels_near(pcm,hwparams,
				      &alsa_capture_format[card].channels);
  alsa_play_format[card].capture_channels=alsa_capture_format[card].channels;
  rda->syslog(LOG_INFO,"  Aggregate Channels = %u",
	 alsa_capture_format[card].channels);

  //
  // Buffer Size
  //
  alsa_capture_format[card].periods=rda->config()->alsaPeriodQuantity();
  snd_pcm_hw_params_set_periods_near(pcm,hwparams,
				     &alsa_capture_format[card].periods,&dir);
  rda->syslog(LOG_INFO,
			"  Periods = %u",alsa_capture_format[card].periods);
  alsa_capture_format[card].buffer_size=
    alsa_capture_format[card].periods*rda->config()->alsaPeriodSize();
  snd_pcm_hw_params_set_buffer_size_near(pcm,hwparams,
	       			 &alsa_capture_format[card].buffer_size);
  rda->syslog(LOG_INFO,"  BufferSize = %u frames",
	 (unsigned)alsa_capture_format[card].buffer_size);

  //
  // Fire It Up
  //
  if((err=snd_pcm_hw_params(pcm,hwparams))<0) {
    rda->syslog(LOG_WARNING,
			  "  Device Error: %s,",snd_strerror(err));
    rda->syslog(LOG_WARNING,
			  "  aborting initialization of device.");
    return false;
  }
  rda->syslog(LOG_INFO,"  Device started successfully");
  switch(alsa_capture_format[card].format) {
  case SND_PCM_FORMAT_S16_LE:
    alsa_capture_format[card].card_buffer_size=
      alsa_capture_format[card].buffer_size*
      alsa_capture_format[card].channels*2;
    break;

  case SND_PCM_FORMAT_S32_LE:
    alsa_capture_format[card].card_buffer_size=
      alsa_capture_format[card].buffer_size*
      alsa_capture_format[card].channels*4;
    break;

  default:
    break;
  }
  alsa_capture_format[card].card_buffer=
    new char[alsa_capture_format[card].card_buffer_size];
  alsa_capture_format[card].passthrough_buffer=
    new char[alsa_capture_format[card].card_buffer_size];
  alsa_capture_format[card].pcm=pcm;
  alsa_capture_format[card].card=card;
  //
  // Set Wake-up Timing
  //
  snd_pcm_sw_params_alloca(&swparams);
  snd_pcm_sw_params_current(pcm,swparams);
  snd_pcm_sw_params_set_avail_min(pcm,swparams,rda->config()->alsaPeriodSize());
  if((err=snd_pcm_sw_params(pcm,swparams))<0) {
    rda->syslog(LOG_WARNING,
			  "ALSA Device %s: %s",(const char *)dev.toUtf8(),
	   snd_strerror(err));
    return false;
  }

  //
  // Start the Callback
  //
  pthread_attr_init(&pthread_attr);
/*
  if(use_realtime) {
    pthread_attr_setschedpolicy(&pthread_attr,SCHED_FIFO);
  }
*/
  alsa_capture_format[card].exiting = false;
  pthread_create(&alsa_capture_format[card].thread,&pthread_attr,
		 AlsaCaptureCallback,&alsa_capture_format[card]);
  return true;
}


bool DriverAlsa::AlsaStartPlayDevice(QString &dev,int card,snd_pcm_t *pcm)
{
  snd_pcm_hw_params_t *hwparams;
  snd_pcm_sw_params_t *swparams;
  int dir;
  int err;
  pthread_attr_t pthread_attr;
  unsigned sr;

  memset(&alsa_play_format[card],0,sizeof(struct alsa_format));

  snd_pcm_hw_params_alloca(&hwparams);
  snd_pcm_hw_params_any(pcm,hwparams);

  rda->syslog(LOG_INFO,"Starting ALSA Play Device %s:",
	 (const char *)dev.toUtf8());

  //
  // Access Type
  //
  if(snd_pcm_hw_params_test_access(pcm,hwparams,
				   SND_PCM_ACCESS_RW_INTERLEAVED)<0) {
    rda->syslog(LOG_WARNING,
			  "  Interleaved access not supported,");
    rda->syslog(LOG_WARNING,
			  "  aborting initialization of device.");
    return false;
  }
  snd_pcm_hw_params_set_access(pcm,hwparams,SND_PCM_ACCESS_RW_INTERLEAVED);

  //
  // Sample Format
  //
  if(snd_pcm_hw_params_test_format(pcm,hwparams,SND_PCM_FORMAT_S32_LE)==0) {
    alsa_play_format[card].format=SND_PCM_FORMAT_S32_LE;
    rda->syslog(LOG_INFO,"  Format = 32 bit little-endian");
  }
  else {
    if(snd_pcm_hw_params_test_format(pcm,hwparams,SND_PCM_FORMAT_S16_LE)==0) {
      alsa_play_format[card].format=SND_PCM_FORMAT_S16_LE;
      rda->syslog(LOG_INFO,
			    "  Format = 16 bit little-endian");
    }
    else {
      rda->syslog(LOG_WARNING,
	     "  Neither 16 nor 32 bit little-endian formats available,");
      rda->syslog(LOG_WARNING,
			    "  aborting initialization of device.");
      return false;
    }
  }
  snd_pcm_hw_params_set_format(pcm,hwparams,alsa_play_format[card].format);

  //
  // Sample Rate
  //
  sr=systemSampleRate();
  snd_pcm_hw_params_set_rate_near(pcm,hwparams,&sr,&dir);
  if((sr<(systemSampleRate()-RD_ALSA_SAMPLE_RATE_TOLERANCE))||
     (sr>(systemSampleRate()+RD_ALSA_SAMPLE_RATE_TOLERANCE))) {
    rda->syslog(LOG_WARNING,
			  "  Asked for sample rate %u, got %u",
			  systemSampleRate(),sr);
    rda->syslog(LOG_WARNING,
			  "  Sample rate unsupported by device");
    return false;
  }
  alsa_play_format[card].sample_rate=sr;
  rda->syslog(LOG_INFO,"  SampleRate = %u",sr);

  //
  // Channels
  //
  if(rda->config()->alsaChannelsPerPcm()<0) {
    alsa_play_format[card].channels=RD_DEFAULT_CHANNELS*RD_MAX_PORTS;
  }
  else {
    alsa_play_format[card].channels=rda->config()->alsaChannelsPerPcm();
  }
  snd_pcm_hw_params_set_channels_near(pcm,hwparams,
				      &alsa_play_format[card].channels);
  rda->syslog(LOG_INFO,"  Aggregate Channels = %u",
			alsa_play_format[card].channels);
  //
  // Buffer Size
  //
  alsa_play_format[card].periods=rda->config()->alsaPeriodQuantity();
  snd_pcm_hw_params_set_periods_near(pcm,hwparams,
				     &alsa_play_format[card].periods,&dir);
  rda->syslog(LOG_INFO,
			"  Periods = %u",alsa_play_format[card].periods);
  alsa_play_format[card].buffer_size=
    alsa_play_format[card].periods*rda->config()->alsaPeriodSize();
  snd_pcm_hw_params_set_buffer_size_near(pcm,hwparams,
					 &alsa_play_format[card].buffer_size);
  rda->syslog(LOG_INFO,"  BufferSize = %u frames",
	 (unsigned)alsa_play_format[card].buffer_size);

  //
  // Fire It Up
  //
  if((err=snd_pcm_hw_params(pcm,hwparams))<0) {
    rda->syslog(LOG_WARNING,
			  "  Device Error: %s,",snd_strerror(err));
    rda->syslog(LOG_ERR,
			  "  aborting initialization of device.");
    return false;
  }
  rda->syslog(LOG_INFO,"  Device started successfully");
  switch(alsa_play_format[card].format) {
  case SND_PCM_FORMAT_S16_LE:
    alsa_play_format[card].card_buffer_size=
      alsa_play_format[card].buffer_size*alsa_play_format[card].channels*2;
    break;

  case SND_PCM_FORMAT_S32_LE:
    alsa_play_format[card].card_buffer_size=
      alsa_play_format[card].buffer_size*alsa_play_format[card].channels*4;
    break;

  default:
    break;
  }
  alsa_play_format[card].card_buffer=
    new char[alsa_play_format[card].card_buffer_size];
  alsa_play_format[card].passthrough_buffer=
    new char[alsa_play_format[card].card_buffer_size];
  alsa_play_format[card].pcm=pcm;
  alsa_play_format[card].card=card;

  //
  // Set Wake-up Timing
  //
  snd_pcm_sw_params_alloca(&swparams);
  snd_pcm_sw_params_current(pcm,swparams);
  snd_pcm_sw_params_set_avail_min(pcm,swparams,rda->config()->alsaPeriodSize());
  if((err=snd_pcm_sw_params(pcm,swparams))<0) {
    rda->syslog(LOG_WARNING,"ALSA Device %s: %s",
	   (const char *)dev.toUtf8(),snd_strerror(err));
    return false;
  }

  //
  // Start the Callback
  //
  pthread_attr_init(&pthread_attr);
  /*
  if(use_realtime) {
    pthread_attr_setschedpolicy(&pthread_attr,SCHED_FIFO);
  }
  */
  alsa_play_format[card].exiting = false;
  pthread_create(&alsa_play_format[card].thread,&pthread_attr,
		 AlsaPlayCallback,&alsa_play_format[card]);
  return true;
}


int DriverAlsa::GetAlsaOutputStream(int card)
{
  for(int i=0;i<RD_MAX_STREAMS;i++) {
    if(alsa_play_ring[card][i]==NULL) {
      alsa_play_ring[card][i]=new RDRingBuffer(RINGBUFFER_SIZE);
      return i;
    }
  }
  return -1;
}


void DriverAlsa::FreeAlsaOutputStream(int card,int stream)
{
  delete alsa_play_ring[card][stream];
  alsa_play_ring[card][stream]=NULL;
}


void DriverAlsa::EmptyAlsaInputStream(int card,int stream)
{
  unsigned n=alsa_record_ring[card][stream]->
    read((char *)alsa_wave_buffer,alsa_record_ring[card][stream]->
	 readSpace());
  WriteAlsaBuffer(card,stream,alsa_wave_buffer,n);
}


void DriverAlsa::WriteAlsaBuffer(int card,int stream,int16_t *buffer,unsigned len)
{
  ssize_t s;
  unsigned char mpeg[2048];
  unsigned frames;
  unsigned n;

  frames=len/(2*alsa_record_wave[card][stream]->getChannels());
  alsa_samples_recorded[card][stream]+=frames;
  switch(alsa_record_wave[card][stream]->getFormatTag()) {
  case WAVE_FORMAT_PCM:
    switch(alsa_record_wave[card][stream]->getBitsPerSample()) {
    case 16:   // PCM16
      alsa_record_wave[card][stream]->writeWave(buffer,len);
      break;

    case 24:   // PCM24
      for(unsigned i=0;i<(len/2);i++) {
	alsa_wave24_buffer[3*i]=0;      // FIXME: we lose eight bits here!
	alsa_wave24_buffer[3*i+1]=((uint8_t *)buffer)[2*i];
	alsa_wave24_buffer[3*i+2]=((uint8_t *)buffer)[2*i+1];
      }
      alsa_record_wave[card][stream]->writeWave(alsa_wave24_buffer,3*len/2);
      break;
    }
    break;

  case WAVE_FORMAT_MPEG:
#ifdef HAVE_TWOLAME
    for(unsigned i=0;i<frames;i+=1152) {
      if((i+1152)>frames) {
	n=frames-i;
      }
      else {
	n=1152;
      }
      if((s=twolame_encode_buffer_interleaved(twolame_lameopts[card][stream],
		   buffer+i*alsa_record_wave[card][stream]->getChannels(),
					      n,mpeg,2048))>=0) {
	alsa_record_wave[card][stream]->writeWave(mpeg,s);
      }
      else {
	rda->syslog(LOG_WARNING,
			      "TwoLAME encode error, card: %d, stream: %d",
	       card,stream);
      }
    }
#endif  // HAVE_TWOLAME
    break;
  }
}


void DriverAlsa::FillAlsaOutputStream(int card,int stream)
{
  unsigned mpeg_frames=0;
  unsigned frame_offset=0;
  int m=0;
  int n=0;
  double ratio=0.0;
  int free=(alsa_play_ring[card][stream]->writeSpace()-1);
  if(free<=0) {
    return;
  }
  ratio=(double)alsa_play_format[card].sample_rate/
    (double)alsa_play_wave[card][stream]->getSamplesPerSec();
  switch(alsa_play_wave[card][stream]->getFormatTag()) {
  case WAVE_FORMAT_PCM:
  case WAVE_FORMAT_VORBIS:
    switch(alsa_play_wave[card][stream]->getBitsPerSample()) {
    case 16:   // PCM16
      free=(int)((double)free/ratio)/(2*alsa_output_channels[card][stream])*
	      (2*alsa_output_channels[card][stream]);
      n=alsa_play_wave[card][stream]->readWave(alsa_wave_buffer,free);
      if(n!=free) {
	alsa_eof[card][stream]=true;
	alsa_stop_timer[card][stream]->stop();
      }
      break;

    case 24:   // PCM24
      free=(int)((double)free/ratio)/(2*alsa_output_channels[card][stream])*
	      (2*alsa_output_channels[card][stream]);
      n=2*alsa_play_wave[card][stream]->readWave(alsa_wave24_buffer,3*free/2)/3;
      if(n!=free) {
	alsa_eof[card][stream]=true;
	alsa_stop_timer[card][stream]->stop();
	break;
      }
      for(int i=0;i<n/2;i++) {
	((uint8_t *)alsa_wave_buffer)[2*i]=alsa_wave24_buffer[3*i+1];
	((uint8_t *)alsa_wave_buffer)[2*i+1]=alsa_wave24_buffer[3*i+2];
      }
    }
    break;

  case WAVE_FORMAT_MPEG:
#ifdef HAVE_MAD
    mpeg_frames=free/(2304*alsa_output_channels[card][stream]);
    free=mpeg_frames*2304*alsa_output_channels[card][stream];
    for(unsigned i=0;i<mpeg_frames;i++) {
      m=alsa_play_wave[card][stream]->
	readWave(mad_mpeg[card][stream]+mad_left_over[card][stream],
		 mad_frame_size[card][stream]);
      if(m==mad_frame_size[card][stream]) {
	mad_stream_buffer(&mad_stream[card][stream],mad_mpeg[card][stream],
			  m+mad_left_over[card][stream]);
	while(mad_frame_decode(&mad_frame[card][stream],
			       &mad_stream[card][stream])==0) {
	  mad_synth_frame(&mad_synth[card][stream],&mad_frame[card][stream]);
	  n+=(2*alsa_output_channels[card][stream]*
	      mad_synth[card][stream].pcm.length);
	  for(int j=0;j<mad_synth[card][stream].pcm.length;j++) {
	    for(int k=0;k<mad_synth[card][stream].pcm.channels;k++) {
	      alsa_wave_buffer[frame_offset+
			       j*mad_synth[card][stream].pcm.channels+k]=
		(int16_t)(32768.0*mad_f_todouble(mad_synth[card][stream].
					       pcm.samples[k][j]));
	    }
	  }
	  frame_offset+=(mad_synth[card][stream].pcm.length*
			 mad_synth[card][stream].pcm.channels);
	}
      }
      else {  // End-of-file, read out last samples
	if(!alsa_eof[card][stream]) {
	  memset(mad_mpeg[card][stream]+mad_left_over[card][stream],0,
		 MAD_BUFFER_GUARD);
	  mad_stream_buffer(&mad_stream[card][stream],
			    mad_mpeg[card][stream],
			    MAD_BUFFER_GUARD+mad_left_over[card][stream]);
	  if(mad_frame_decode(&mad_frame[card][stream],
			      &mad_stream[card][stream])==0) {
	    mad_synth_frame(&mad_synth[card][stream],
			    &mad_frame[card][stream]);
	    n+=(alsa_output_channels[card][stream]*
		mad_synth[card][stream].pcm.length);
	    for(int j=0;j<mad_synth[card][stream].pcm.length;j++) {
	      for(int k=0;k<mad_synth[card][stream].pcm.channels;k++) {
		alsa_wave_buffer[frame_offset+
				 j*mad_synth[card][stream].pcm.channels+k]=
		  (int16_t)(32768.0*mad_f_todouble(mad_synth[card][stream].
						 pcm.samples[k][j]));
	      }
	    }
	  }
	}
	alsa_eof[card][stream]=true;
	alsa_stop_timer[card][stream]->stop();
	continue;
      }
      mad_left_over[card][stream]=
	mad_stream[card][stream].bufend-mad_stream[card][stream].next_frame;
      memmove(mad_mpeg[card][stream],mad_stream[card][stream].next_frame,
	      mad_left_over[card][stream]);
    }
#endif  // HAVE_MAD
    break;
  }
  alsa_play_ring[card][stream]->write((char *)alsa_wave_buffer,n);
}
#endif  // ALSA


#ifdef ALSA
void DriverAlsa::AlsaClock()
{
  for(int i=0;i<RD_MAX_CARDS;i++) {
    if(hasCard(i)) {
      for(int j=0;j<RD_MAX_STREAMS;j++) {
	if(alsa_stopping[i][j]) {
	  alsa_stopping[i][j]=false;
	  alsa_eof[i][j]=false;
	  alsa_playing[i][j]=false;
	  statePlayUpdate(i,j,2);
	}
	if(alsa_playing[i][j]) {
	  FillAlsaOutputStream(i,j);
	}
      }
      for(int j=0;j<RD_MAX_PORTS;j++) {
	if(alsa_recording[i][j]) {
	  EmptyAlsaInputStream(i,j);
	}
      }
    }
  }
}
#endif  // ALSA
