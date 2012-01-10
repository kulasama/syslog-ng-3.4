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
  
#include "dgroup.h"
#include "misc.h"
#include "stats.h"
#include "messages.h"

#include <sys/time.h>

typedef struct _LogDestGroup
{
  LogPipe super;
  gchar *name;
  LogPipe *contents;
  StatsCounterItem *processed_messages;
} LogDestGroup;

static gboolean
log_dest_group_init(LogPipe *s)
{
  LogDestGroup *self = (LogDestGroup *) s;

  stats_lock();
  stats_register_counter(0, SCS_DESTINATION | SCS_GROUP, self->name, NULL, SC_TYPE_PROCESSED, &self->processed_messages);
  stats_unlock();
  return TRUE;
}

static gboolean
log_dest_group_deinit(LogPipe *s)
{
  LogDestGroup *self = (LogDestGroup *) s;
  gboolean success = TRUE;

  stats_lock();
  stats_unregister_counter(SCS_DESTINATION | SCS_GROUP, self->name, NULL, SC_TYPE_PROCESSED, &self->processed_messages);
  stats_unlock();
  return success;
}

static void
log_dest_group_queue(LogPipe *s, LogMessage *msg, const LogPathOptions *path_options, gpointer user_data)
{
  LogDestGroup *self = (LogDestGroup *) s;
  
  /* send message to the embedded drivers */
  //  log_msg_add_ack(msg, path_options);
  //  log_pipe_queue(self->contents, log_msg_ref(msg), path_options);

  /* forward message to the next hop */

  stats_counter_inc(self->processed_messages);
  log_pipe_forward_msg(s, msg, path_options);
}

static void
log_dest_group_free(LogPipe *s)
{
  LogDestGroup *self = (LogDestGroup *) s;
  
  //log_pipe_unref(self->contents);
  g_free(self->name);
  log_pipe_free_method(s);
}

LogPipe *
log_dest_group_new(gchar *name)
{
  LogDestGroup *self = g_new0(LogDestGroup, 1);

  log_pipe_init_instance(&self->super);
  self->super.init = log_dest_group_init;
  self->super.deinit = log_dest_group_deinit;
  self->super.queue = log_dest_group_queue;
  self->super.free_fn = log_dest_group_free;
  self->name = g_strdup(name);

  return &self->super;
}
