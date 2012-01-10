/*
 * Copyright (c) 2002-2010 BalaBit IT Ltd, Budapest, Hungary
 * Copyright (c) 1998-2010 Balázs Scheidler
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
  
#include "sgroup.h"
#include "misc.h"
#include "messages.h"
#include "stats.h"
#include "afinter.h"

#include <time.h>
#include <string.h>

typedef struct _LogSourceGroup
{
  LogPipe super;
  gchar *name;
  gint name_len;
  StatsCounterItem *processed_messages;
} LogSourceGroup;


static gboolean
log_source_group_init(LogPipe *s)
{
  LogSourceGroup *self = (LogSourceGroup *) s;

  stats_lock();
  stats_register_counter(0, SCS_SOURCE | SCS_GROUP, self->name, NULL, SC_TYPE_PROCESSED, &self->processed_messages);
  stats_unlock();
  return TRUE;
}

static gboolean
log_source_group_deinit(LogPipe *s)
{
  LogSourceGroup *self = (LogSourceGroup *) s;

  stats_lock();
  stats_unregister_counter(SCS_SOURCE | SCS_GROUP, self->name, NULL, SC_TYPE_PROCESSED, &self->processed_messages);
  stats_unlock();
  return TRUE;
}

static void
log_source_group_queue(LogPipe *s, LogMessage *msg, const LogPathOptions *path_options, gpointer user_data)
{
  LogSourceGroup *self = (LogSourceGroup *) s;
  GlobalConfig *cfg = log_pipe_get_config(s);
  
  log_msg_set_value(msg, LM_V_SOURCE, self->name, self->name_len);

  if (msg->flags & LF_LOCAL)
    afinter_postpone_mark(cfg->mark_freq);
  log_pipe_forward_msg(s, msg, path_options);
  stats_counter_inc(self->processed_messages);
}

static void
log_source_group_free(LogPipe *s)
{
  LogSourceGroup *self = (LogSourceGroup *) s;
  
  g_free(self->name);
  log_pipe_free_method(s);
}

LogPipe *
log_source_group_new(gchar *name)
{
  LogSourceGroup *self = g_new0(LogSourceGroup, 1);

  log_pipe_init_instance(&self->super);  
  self->super.init = log_source_group_init;
  self->super.deinit = log_source_group_deinit;
  self->super.queue = log_source_group_queue;
  self->super.free_fn = log_source_group_free;

  self->name = g_strdup(name);
  //self->name_len = strlen(self->name);

  return &self->super;
}
