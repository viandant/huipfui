/* in2hid is the counterpart of hid2out.
 *  
 *   Copyright (C) 2016  Pizzocel
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include "hipipe.h"

int event_in(channel_event_t *chev) {
  const char evformat[] = " #%x %lx %lx %"SCNx16" %"SCNx16" %"SCNx32;
  char * n;
  int res;
  res =  scanf(evformat,
	       &chev->channel,
	       &chev->ev.time.tv_sec, &chev->ev.time.tv_usec,
	       &chev->ev.type, &chev->ev.code, &chev->ev.value);
  if ((res < 6) && (res != EOF))
    {
      scanf("%ms", &n);
      printf("Syntax error at '%s'\n", n);
      free(n);
    };
  return res;
};

int set_type_bit(int type) {
  int res;
  switch (type) {
  case EV_KEY:
    res = UI_SET_KEYBIT;
    break;
  case EV_REP:
    res = 0;
    break;
  case EV_REL:
    res = UI_SET_RELBIT;
    break;
  case EV_ABS:
    res = UI_SET_ABSBIT;
    break;
  case EV_MSC:
    res = UI_SET_MSCBIT;
    break;
  case EV_LED:
    res = UI_SET_LEDBIT;
    break;
  case EV_SND:
    res = UI_SET_SNDBIT;
    break;
  case EV_FF:
    res = UI_SET_FFBIT;
    break;
  case EV_SW:
    res = UI_SET_SWBIT;
    break;
  default:
    printf("Type %d unknown\n", type);
    exit(3);
    break;
  };
  return res;
};

int fds[DEV_MAX];

void finish_channel_config(unsigned int channel,
			   struct uinput_user_dev * uidev) {
  if (fds[channel] > 0) {
    if (write(fds[channel], uidev, sizeof(*uidev)) < 0)
      exit(4);
    if (ioctl(fds[channel], UI_DEV_CREATE) < 0)
      exit(5);
  };
};

int main(){
  struct uinput_user_dev uidev;
  //struct input_event     ev;
  unsigned int channel;
  channel_event_t        chev;
  
  memset(&uidev, 0, sizeof(uidev));
  uidev.id.bustype = BUS_USB;
  uidev.id.vendor  = 0x1;
  uidev.id.product = 0x1;
  uidev.id.version = 1;
  unsigned int type = 0;
  unsigned int code = 0;
  channel = 0;
  
  int header = 1;
  int set_type_bit_c;
  while(header) {
    char mark;
    struct input_absinfo absinfo;
    scanf("%c",&mark);
    switch (mark){
    case '#':
      finish_channel_config(channel, &uidev);
      scanf("%x", &channel);
      fds[channel] = open("/dev/uinput", O_RDWR );
      if(fds[channel] < 0) {
	exit(1);
      };
      snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "%s%02d\0",
	       "in2hid",channel);
      break;
    case '!':
      scanf("%x", &type);
      if (ioctl(fds[channel], UI_SET_EVBIT, type) < 0)
      	exit(2);
      break;
    case '*':
      scanf("%x", &code);
      if (set_type_bit_c = set_type_bit(type))
	if (ioctl(fds[channel], set_type_bit_c, code) < 0)
	  exit(2);
      break;
    case '~':
      scanf("%"SCNx32" %"SCNx32" %"SCNx32,
	    &absinfo.minimum, &absinfo.maximum, &absinfo.resolution);
      uidev.absmin[code] = absinfo.minimum;
      uidev.absmax[code] = absinfo.maximum;
      // This is currently missing from uinput.h
      // uidev.absres[code] = absinfo.resolution;
      uidev.absfuzz[code] = 0;
      uidev.absflat[code] = 0;
      break;
    case '_':
      header = 0;
      finish_channel_config(channel,&uidev);
      printf("Reading header completed. Virtual input devices have been created.\n");
      break;
    };
  };
  int leftmeta = 0;
  int recently_leftalt = 0;
  int leftalt  = 0;
  memset(&chev.ev, 0, sizeof(chev.ev));
  int cont = 1;
  sleep(1);

  int res;
  while ((res = event_in(&chev)) != EOF) {
    if (res == 6) {
      while(!write(fds[chev.channel], &chev.ev, sizeof(chev.ev))){};
    };
  };
  return 0;
};


