/* hid2out grabs the input devices as defined in the config file,
 *   reads the input events and outputs representations of them
 *   to standard output. 
 *   The output is entnded to be read be the program in2hid.
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

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <libconfig.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include "hipipe.h"

//We borrow some code from udev-builtin-input_id.c
#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

typedef unsigned long *bitmap_t;
typedef unsigned long evtype_bitmap_t [NBITS(EV_MAX)];
typedef unsigned long keycode_bitmap_t [NBITS(KEY_MAX)];
typedef unsigned long relcode_bitmap_t [NBITS(REL_MAX)];
typedef unsigned long abscode_bitmap_t [NBITS(ABS_MAX)];
typedef unsigned long msccode_bitmap_t [NBITS(MSC_MAX)];
typedef unsigned long ledcode_bitmap_t [NBITS(LED_MAX)];
typedef unsigned long swcode_bitmap_t  [NBITS(SW_MAX)];
typedef unsigned long repcode_bitmap_t [NBITS(REP_MAX)];

//typedef unsigned long * evtype_bitmap_t;

//Maximum path length
#define PATH_MAX 1024
#define KEYWORD_MAX 80
config_t cfg;
config_setting_t *setting;
config_setting_t *eventmaps = NULL;
config_setting_t *eventmap = NULL;
char config_file_path_buffer[PATH_MAX];
char * config_file_path = NULL;
char config_file_rel_path[] = "/.config/hid2out.conf";

const char * dev_paths[DEV_MAX];
int event_fd[DEV_MAX];



void perror_exit (int sig, char * format,...)
{
  va_list args;
  va_start(args,format);
  vfprintf (stderr,format,args);
  va_end(args);
  fprintf (stderr,"\nexiting...(%d)\n", sig);
  exit(sig);
}

/* Works like printf but without a buffer.
   This saves us from having to use stdbuf when piping. */
size_t out(const char * format,...)
{
  va_list args;
  char * outbuf = NULL;
  size_t res = 0;
  va_start(args, format);
  if (res = vasprintf(&outbuf,format,args)) {
    res = write(STDOUT_FILENO, outbuf, res);
    free(outbuf);
  };
  va_end(args);
  return res;
};

const char evformat[] = "#%x\n%lx\n%lx\n%"PRIx16"\n%"PRIx16"\n%"PRIx32"\n";
int event_out(channel_event_t * chev) {
  return out(evformat,
	     chev->channel,
	     chev->ev.time.tv_sec, chev->ev.time.tv_usec,
	     chev->ev.type,chev->ev.code,chev->ev.value);
};


/* ********************************************************************************
   Getting features of the input devices on writing them to output
   ******************************************************************************** */

void get_range(int fd, unsigned int abs) {
  struct input_absinfo absinfo;

  ioctl(fd,EVIOCGABS(abs),&absinfo);
  out("~%"PRIx32" %"PRIx32" %"PRIx32"\n", absinfo.minimum, absinfo.maximum,
	 absinfo.resolution);
};

void get_subfeatures(int fd, unsigned int evtype, bitmap_t bitmap, unsigned int max,
		     int is_abs) {
  unsigned int j=0;
  ioctl(fd,EVIOCGBIT(evtype, max), bitmap);
  for(j=0; j < max; j++) {
    if(test_bit(j,bitmap)) {
      out("*%x\n", j);
      if(is_abs)
	get_range(fd, j);
    };
  };
};

void get_features(int fd) {
  evtype_bitmap_t evtype_bitmap;
  keycode_bitmap_t keycode_bitmap;
  relcode_bitmap_t relcode_bitmap;
  abscode_bitmap_t abscode_bitmap;
  msccode_bitmap_t msccode_bitmap;
  ledcode_bitmap_t ledcode_bitmap;
  swcode_bitmap_t swcode_bitmap;
  repcode_bitmap_t repcode_bitmap;

  ioctl(fd,EVIOCGBIT(0,EV_MAX),evtype_bitmap);
  int i=0;
  int j=0;
  for(i=0; i < EV_MAX; i++) {
    if(test_bit(i,evtype_bitmap)) {
      out("!%x\n", i);
      switch(i) {
      case EV_SYN:
	break;
      case EV_KEY:
	get_subfeatures(fd, i, keycode_bitmap, KEY_MAX, 0);
	break;
      case EV_REL:
	get_subfeatures(fd, i, relcode_bitmap, REL_MAX, 0);
	break;
      case EV_ABS:
	get_subfeatures(fd, i, abscode_bitmap, ABS_MAX, 1);
	break;
      case EV_MSC:
	get_subfeatures(fd, i, msccode_bitmap, MSC_MAX, 0);
	break;
      case EV_SW:
	get_subfeatures(fd, i, swcode_bitmap, SW_MAX, 0);
	break;
      case EV_LED:
	get_subfeatures(fd, i, ledcode_bitmap, LED_MAX, 0);
	break;
      case EV_SND:
	break;
      case EV_REP:
	get_subfeatures(fd, i, repcode_bitmap, REP_MAX, 0);
	break;
      case EV_FF:
	break;
      case EV_PWR:
	break;
      case EV_FF_STATUS:
	break;
      default:
	break;
      };
    };
  };
};

/* ********************************************************************************
   Handling event maps
   ******************************************************************************** */

int val_match(config_setting_t * config_range,
	      long int val) {
  int res = 1;
  int type;
  config_setting_t * min;
  config_setting_t * max;
  if (config_range) {
    switch (config_setting_type(config_range)) {
    case CONFIG_TYPE_INT:
      res &= config_setting_get_int(config_range) == val;
      break;
    case CONFIG_TYPE_INT64:
      res &= config_setting_get_int64(config_range) == val;
      break;
    case CONFIG_TYPE_LIST:
      min = config_setting_get_elem(config_range,0);
      max = config_setting_get_elem(config_range,1);
      if (min) {
	switch (config_setting_type(min)) {
	case CONFIG_TYPE_INT:
	  res &= config_setting_get_int(min) <= val;
	  break;
	case CONFIG_TYPE_INT64:
	  res &= config_setting_get_int64(min) <= val;
	  break;
	default:
	  break;
	};
      };
      if (max) {
	switch (config_setting_type(max)) {
	case CONFIG_TYPE_INT:
	  res &= val <= config_setting_get_int(max);
	  break;
	case CONFIG_TYPE_INT64:
	  res &= val <= config_setting_get_int64(min);
	  break;
	default:
	  break;
	};
      default:
	break;
      };
    };
  };
  return res;
};

/* Match an event against the left hand side
   of an event map rule. */
int event_match(config_setting_t * config_event,
		channel_event_t * chev) {
  int res = 0;
  if (config_event)
    res =
      val_match(config_setting_get_elem(config_event,0), chev->channel) &&
      val_match(config_setting_get_elem(config_event,1), chev->ev.type) &&
      val_match(config_setting_get_elem(config_event,2), chev->ev.code) &&
      val_match(config_setting_get_elem(config_event,3), chev->ev.value);
  return res;
};

long long int set_from_config(config_setting_t * config_action,
			      unsigned int index,
			      long long int val) {
  if (config_action) {
    config_setting_t * elem = config_setting_get_elem(config_action, index);
    if (elem)
      switch (config_setting_type(elem)) {
      case CONFIG_TYPE_INT:
	val = config_setting_get_int(elem);
	break;
      case CONFIG_TYPE_INT64:
	val = config_setting_get_int64(elem);
	break;
      default:
	break;
      };
  };
  return val;
};

/* Execute the action, which has been looked up
   in the table for an event.
   If the action is NULL, this means that nothing was
   found. In this case the default action is executed,
   which is to write the representation of the
   event to the standard output.
   If the action is an empty list, the event
   is ignored an nothing is done. */

int event_do(config_setting_t * config_action,
	      channel_event_t * chev	      ) {
  const char * keyword;
  int cont = 1;
  if (config_action) {
    keyword = config_setting_get_string_elem(config_action, 0);
    if (keyword)
      if (strcasecmp("exec\0", keyword) == 0) {
	system(config_setting_get_string_elem(config_action,1));
      }
      else if(strcasecmp("exit\0", keyword) == 0) {
	cont = 0;
      }
      else if (strcasecmp("send\0", keyword) == 0) {
	chev->channel  = set_from_config(config_action, 1, chev->channel);
	chev->ev.type  = set_from_config(config_action, 2, chev->ev.type);
	chev->ev.code  = set_from_config(config_action, 3, chev->ev.code);
	chev->ev.value = set_from_config(config_action, 4, chev->ev.value);
	event_out(chev);
      } else if (strcasecmp("switch\0", keyword) == 0) {
	const char * mapname = config_setting_get_string_elem(config_action,1);
	config_setting_t * new_map = NULL;
	if (mapname)
	  new_map = config_setting_lookup(eventmaps, mapname);
	if (new_map) eventmap = new_map;
	//TODO: else warning
      } else {
	event_out(chev);
      }}
  else
    event_out(chev);
  return cont;
};

/* Find the action in the current event table
   and return it. NULL if no action is
   associated to the event. */
config_setting_t *  event_action(channel_event_t * chev) {
  config_setting_t * res = NULL;
  config_setting_t * curr_pair = NULL;
  if (eventmap) {
    int i = 0;
    int map_size = config_setting_length( eventmap);
    while (i < map_size && res == NULL) {
      curr_pair = config_setting_get_elem(eventmap,i);
      if (curr_pair) {
	if (event_match(config_setting_get_elem(curr_pair,0), chev)) {
	  res = config_setting_get_elem(curr_pair,1);
	};
      };
      i++;
    };
  };
  return res;
};

/* Find the action for the event  in the current event table
   and execute it.*/
int event_do_action(channel_event_t * chev) {
  return event_do(event_action(chev),chev);
};


/* ********************************************************************************
   MAIN
   ******************************************************************************** */

int main(int argc, char **argv) {
  /* Configuration */
  channel_event_t chev;
  if (argc > 2)
    perror_exit(101, "USAGE: hid2out [<config file>]\n");
  if (argc == 2)
    config_file_path = argv[1];
  else {
    char * home = getenv("HOME");
    char * p = stpncpy(config_file_path_buffer, home, PATH_MAX);
    p = stpncpy(p, config_file_rel_path, PATH_MAX - (p - config_file_path_buffer));
    if (*p != '\0')
      perror_exit(100, "Path to config file too long.");
    config_file_path = config_file_path_buffer;
  };
  config_init(&cfg);
  if(! config_read_file(&cfg, config_file_path) )
  {
    perror_exit(1,"%s:%d - %s", config_error_file(&cfg),
		config_error_line(&cfg), config_error_text(&cfg));
    config_destroy(&cfg);
    return(EXIT_FAILURE);
  }

  config_setting_t *root_setting;
  config_setting_t *first_setting;
  const char * firstname;

  setting = config_lookup(&cfg,"input_devices");
  if (!setting)
    perror_exit(98,"List of input devices not configured.");
  if (config_setting_type(setting) != CONFIG_TYPE_LIST)
    perror_exit(97,"Input devices must be given using list syntax ( \"<device0>\", ... ).");

  int dev_count = config_setting_length(setting);
  if (dev_count > DEV_MAX)
    perror_exit(96,"Can't handle more then %d input devices.", DEV_MAX);

  int index = 0;
  for(index = 0; index < dev_count; index++) {
    dev_paths[index] = config_setting_get_string_elem(setting,index);
  };

  eventmaps = config_lookup(&cfg,"eventmaps");
  if (setting) {
    eventmap = config_setting_lookup(eventmaps, "main");
  };

  // sleep a little, so we need not see the last events of the input
  // that started this program.
  sleep(1);

  /* Open each configured input device and write
     its heatures to the header of the output */
  fd_set readfds;
  fd_set writefds;
  fd_set exceptfds;
  fd_set tmp_readfds;
  fd_set tmp_writefds;
  fd_set tmp_exceptfds;
  struct input_event  iev;
  memset(&iev, 0, sizeof(iev));
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  FD_ZERO(&exceptfds);
  int i = 0;
  int max_fd = -1;
  for(index = 0; index < dev_count; index++) {
    event_fd[i] = open(dev_paths[index], O_RDONLY);
    if (event_fd[i] >= 0) {
      FD_SET(event_fd[i], &readfds);
      ioctl (event_fd[i], EVIOCGRAB , 1);
      if (event_fd[i] > max_fd)
	max_fd = event_fd[i];
      out("#%x\n",i);
      get_features(event_fd[i]);
      i++;
    };
  };

  /* Mark the end of the header */
  out("_\n");

  /* Read the events from the input devices and perform
     the action according to the eventmaps */
  int cont = 1;
  while(cont){
    tmp_readfds = readfds;
    tmp_writefds = writefds;
    tmp_exceptfds = exceptfds;
    select(max_fd+1, &tmp_readfds, &tmp_writefds, &tmp_exceptfds, NULL);
    i=0;
    while(!FD_ISSET(event_fd[i],&tmp_readfds)) i++;
    ssize_t bytes = read(event_fd[i], &iev, sizeof(iev));
    if (bytes == sizeof(iev)) {
      chev.ev = iev;
      chev.channel = i;
      cont = event_do_action(&chev);
    } else
      cont = 0;
  };
  return 0;
};
