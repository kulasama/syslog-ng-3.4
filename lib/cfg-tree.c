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

#include "cfg-tree.h"
#include "sgroup.h"
#include "dgroup.h"
#include "filter.h"
#include "messages.h"
#include "afinter.h"
#include "stats.h"
#include "logparser.h"
#include "logmpx.h"

#include <string.h>

const gchar *
log_expr_node_get_content_name(gint content)
{
  switch (content)
    {
    case ENC_PIPE:
      return "pipe";
    case ENC_SOURCE:
      return "source";
    case ENC_FILTER:
      return "filter";
    case ENC_PARSER:
      return "parser";
    case ENC_REWRITE:
      return "rewrite";
    case ENC_DESTINATION:
      return "destination";
    default:
      g_assert_not_reached();
      break;
    }
}

const gchar *
log_expr_node_get_layout_name(gint layout)
{
  switch (layout)
    {
    case ENL_SINGLE:
      return "single";
    case ENL_REFERENCE:
      return "reference";
    case ENL_SEQUENCE:
      return "sequence";
    case ENL_JUNCTION:
      return "junction";
    default:
      g_assert_not_reached();
      break;
    }
}


/**
 * log_expr_node_append:
 * @a: first LogExprNode
 * @b: second LogExprNode
 *
 * This function appends @b to @a in a linked list using the ep_next field
 * in LogExprNode.
 **/
void
log_expr_node_append(LogExprNode *a, LogExprNode *b)
{
  a->next = b;
}

LogExprNode *
log_expr_node_append_tail(LogExprNode *a, LogExprNode *b)
{
  if (a)
    {
      LogExprNode *p = a;
      while (p->next)
        p = p->next;
      log_expr_node_append(p, b);
      return a;
    }
  return b;
}

const gchar *
log_expr_node_format_location(LogExprNode *self)
{
  static gchar buf[128];
  LogExprNode *node = self;

  while (node)
    {
      if (self->line || self->column)
        {
          g_snprintf(buf, sizeof(buf), "%s:%d:%d", self->filename ? : "#buffer", self->line, self->column);
          break;
        }
      node = node->parent;
    }
  if (!node)
    strncpy(buf, "#unknown", sizeof(buf));
  return buf;
}

void
log_expr_node_set_name(LogExprNode *self, const gchar *name)
{
  g_free(self->name);
  self->name = g_strdup(name);
}

void
log_expr_node_set_children(LogExprNode *self, LogExprNode *children)
{
  LogExprNode *ep;

  /* we don't currently support setting the children list multiple
   * times. no inherent reason, just the proper free function would
   * need to be written, until then this assert would reveal the case
   * quite fast.
   */

  g_assert(self->children == NULL);

  for (ep = children; ep; ep = ep->next)
    ep->parent = self;

  self->children = children;
}


void
log_expr_node_set_flags(LogExprNode *self, guint32 flags)
{
  self->flags = flags;
}

void
log_expr_node_set_object(LogExprNode *self, gpointer object, GDestroyNotify destroy)
{
  self->object = object;
  self->object_destroy = destroy;
}

void
log_expr_node_set_aux(LogExprNode *self, gpointer aux, GDestroyNotify destroy)
{
  self->aux = aux;
  self->aux_destroy = destroy;
}

/**
 * log_expr_node_new:
 * @type: rule type (RT_*)
 * @name: name of this rule (optional)
 * @items: list of endpoints in this log statement
 * @flags: a combination of LC_* flags as specified by the administrator
 *
 * This function constructs a LogExprNode object which encapsulates a log
 * statement in the configuration, e.g. it has one or more sources, filters
 * and destinations each represented by a LogExprNode object.
 **/
LogExprNode *
log_expr_node_new(gint layout, gint content, const gchar *name, LogExprNode *children, guint32 flags, YYLTYPE *yylloc)
{
  LogExprNode *self = g_new0(LogExprNode, 1);

  self->layout = layout;
  self->content = content;
  self->name = g_strdup(name);
  log_expr_node_set_children(self, children);
  self->flags = flags;
  if (yylloc)
    {
      self->filename = g_strdup(yylloc->level->name);
      self->line = yylloc->first_line;
      self->column = yylloc->first_column;
    }
  return self;
}

/**
 * log_expr_node_free:
 * @self: LogExprNode instance
 *
 * This function frees the LogExprNode object encapsulating a log
 * expression node pointed to by @self.
 **/
void
log_expr_node_free(LogExprNode *self)
{
  LogExprNode *next, *p;

  for (p = self->children ; p; p = next)
    {
      next = p->next;
      log_expr_node_free(p);
    }
  if (self->object && self->object_destroy)
    self->object_destroy(self->object);
  if (self->aux && self->aux_destroy)
    self->aux_destroy(self->aux);
  g_free(self->name);
  g_free(self->filename);
  g_free(self);
}

LogExprNode *
log_expr_node_new_pipe(LogPipe *pipe, YYLTYPE *yylloc)
{
  LogExprNode *node = log_expr_node_new(ENL_SINGLE, ENC_PIPE, NULL, NULL, 0, yylloc);

  log_expr_node_set_object(node, pipe, (GDestroyNotify) log_pipe_unref);
  return node;
}


LogExprNode *
log_expr_node_new_source(const gchar *name, LogExprNode *children, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_SOURCE, name, children, 0, yylloc);
}

LogExprNode *
log_expr_node_new_source_reference(const gchar *name, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_REFERENCE, ENC_SOURCE, name, NULL, 0, yylloc);
}

LogExprNode *
log_expr_node_new_destination(const gchar *name, LogExprNode *children, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_DESTINATION, name, children, 0, yylloc);
}

LogExprNode *
log_expr_node_new_destination_reference(const gchar *name, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_REFERENCE, ENC_DESTINATION, name, NULL, 0, yylloc);
}

LogExprNode *
log_expr_node_new_filter(const gchar *name, LogExprNode *child, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_FILTER, name, child, 0, yylloc);
}

LogExprNode *
log_expr_node_new_filter_reference(const gchar *name, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_REFERENCE, ENC_FILTER, name, NULL, 0, yylloc);
}

LogExprNode *
log_expr_node_new_parser(const gchar *name, LogExprNode *children, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_PARSER, name, children, 0, yylloc);
}

LogExprNode *
log_expr_node_new_parser_reference(const gchar *name, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_REFERENCE, ENC_PARSER, name, NULL, 0, yylloc);
}

LogExprNode *
log_expr_node_new_rewrite(const gchar *name, LogExprNode *children, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_REWRITE, name, children, 0, yylloc);
}

LogExprNode *
log_expr_node_new_rewrite_reference(const gchar *name, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_REFERENCE, ENC_REWRITE, name, NULL, 0, yylloc);
}

LogExprNode *
log_expr_node_new_log(LogExprNode *children, guint32 flags, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_PIPE, NULL, children, flags, yylloc);
}

LogExprNode *
log_expr_node_new_sequence(LogExprNode *children, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_PIPE, NULL, children, 0, yylloc);
}

LogExprNode *
log_expr_node_new_junction(LogExprNode *children, YYLTYPE *yylloc)
{
  return log_expr_node_new(ENL_JUNCTION, ENC_PIPE, NULL, children, 0, yylloc);
}

gint
log_expr_node_lookup_flag(const gchar *flag)
{
  if (strcmp(flag, "catch-all") == 0 || strcmp(flag, "catchall") == 0 || strcmp(flag, "catch_all") == 0)
    return LC_CATCHALL;
  else if (strcmp(flag, "fallback") == 0)
    return LC_FALLBACK;
  else if (strcmp(flag, "final") == 0)
    return LC_FINAL;
  else if (strcmp(flag, "flow_control") == 0 || strcmp(flag, "flow-control") == 0)
    return LC_FLOW_CONTROL;
  msg_error("Unknown log statement flag", evt_tag_str("flag", flag), NULL);
  return 0;
}

/* hash foreach function to add all source objects to catch-all rules */
static void
cfg_tree_add_all_sources(gpointer key, gpointer value, gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  LogExprNode *referring_rule = args[1];
  LogExprNode *rule = (LogExprNode *) value;

  if (rule->content != ENC_SOURCE)
    return;

  /* prepend a source reference */
  referring_rule->children = log_expr_node_append_tail(log_expr_node_new_source_reference(rule->name, NULL), referring_rule->children);
}

gboolean
cfg_tree_compile_node(CfgTree *self, LogExprNode *node,
                      LogPipe **outer_pipe_head, LogPipe **outer_pipe_tail,
                      gboolean flow_controlled_parent);

static gboolean
cfg_tree_compile_single(CfgTree *self, LogExprNode *node,
                        LogPipe **outer_pipe_head, LogPipe **outer_pipe_tail)
{
  LogPipe *pipe;

  g_assert(node->content == ENC_PIPE);

  pipe = node->object;

  if ((pipe->flags & PIF_INLINED) == 0)
    {
      /* first reference to the pipe uses the same instance, further ones will get cloned */
      pipe->flags |= PIF_INLINED;
    }
  else
    {
      /* ok, we are using this pipe again, it needs to be cloned */
      pipe = log_pipe_clone(pipe);
      if (!pipe)
        {
          msg_error("Error cloning pipe into its reference point, probably the element in question is not meant to be used in this situation",
                    evt_tag_str("location", log_expr_node_format_location(node)),
                    NULL);
          goto error;
        }
      pipe->flags |= PIF_INLINED;
    }
  g_ptr_array_add(self->initialized_pipes, log_pipe_ref(pipe));
  pipe->expr_node = node;

  if ((pipe->flags & PIF_SOURCE) == 0)
    *outer_pipe_head = pipe;
  *outer_pipe_tail = pipe;
  return TRUE;

 error:
  return FALSE;
}

static gboolean
cfg_tree_compile_reference(CfgTree *self, LogExprNode *node,
                           LogPipe **outer_pipe_head, LogPipe **outer_pipe_tail, gboolean flow_controlled_parent)
{
  LogExprNode *referenced_node;

  if (!node->object)
    {
      referenced_node = cfg_tree_get_object(self, node->content, node->name);
    }
  else
    referenced_node = node->object;

  if (!referenced_node)
    {
      msg_error("Error resolving reference",
                evt_tag_str("type", log_expr_node_get_content_name(node->content)),
                evt_tag_str("name", node->name),
                evt_tag_str("location", log_expr_node_format_location(node)),
                NULL);
      goto error;
    }

  switch (referenced_node->content)
    {
    case ENC_SOURCE:
      {
        LogMultiplexer *mpx;
        LogPipe *sub_pipe_head = NULL, *sub_pipe_tail = NULL;
        LogPipe *attach_pipe = NULL;

        if (!referenced_node->aux)
          {
            if (!cfg_tree_compile_node(self, referenced_node, &sub_pipe_head, &sub_pipe_tail, flow_controlled_parent))
              goto error;
            log_expr_node_set_aux(referenced_node, log_pipe_ref(sub_pipe_tail), (GDestroyNotify) log_pipe_unref);
          }
        else
          {
            sub_pipe_tail = referenced_node->aux;
          }

        if (!sub_pipe_tail->pipe_next)
          {
            mpx = log_multiplexer_new(0);
            g_ptr_array_add(self->initialized_pipes, &mpx->super);
            log_pipe_append(sub_pipe_tail, &mpx->super);
          }
        else
          {
            mpx = (LogMultiplexer *) sub_pipe_tail->pipe_next;
          }

        attach_pipe = log_pipe_new();
        g_ptr_array_add(self->initialized_pipes, attach_pipe);

        log_multiplexer_add_next_hop(mpx, attach_pipe);
        *outer_pipe_head = NULL;
        *outer_pipe_tail = attach_pipe;
        break;
      }
    case ENC_DESTINATION:
      {
        LogMultiplexer *mpx;
        LogPipe *sub_pipe_head = NULL, *sub_pipe_tail = NULL;

        if (!referenced_node->aux)
          {
            if (!cfg_tree_compile_node(self, referenced_node, &sub_pipe_head, &sub_pipe_tail, flow_controlled_parent))
              goto error;
            log_expr_node_set_aux(referenced_node, log_pipe_ref(sub_pipe_head), (GDestroyNotify) log_pipe_unref);
          }
        else
          {
            sub_pipe_head = referenced_node->aux;
          }

        /* We need a new LogMultiplexer instance for two reasons:

           1) we need to link something into the sequence, all
           reference based destination invocations need a separate
           LogPipe

           2) we have to fork downwards to the destination, it may
           change the message but we need the original one towards
           our next chain
        */

        mpx = log_multiplexer_new(0);
        g_ptr_array_add(self->initialized_pipes, &mpx->super);
        log_multiplexer_add_next_hop(mpx, sub_pipe_head);
        *outer_pipe_head = &mpx->super;
        *outer_pipe_tail = NULL;
        break;
      }
    default:
      return cfg_tree_compile_node(self, referenced_node, outer_pipe_head, outer_pipe_tail, flow_controlled_parent);
    }
  return TRUE;

 error:
  return FALSE;
}

/**
 * cfg_tree_compile_sequence:
 *
 * Construct the sequential part of LogPipe pipeline as specified by
 * the user. The sequential part is where no branches exist, pipes are
 * merely linked to each other. This is in contrast with a "junction"
 * where the processing is forked into different branches. Junctions
 * are built using cfg_tree_compile_junction() above.
 *
 * The configuration is parsed into a series of LogExprNode
 * elements, each giving a reference to a source, filter, parser,
 * rewrite and destination. This function connects these so that their
 * log_pipe_queue() method will dispatch the message correctly (which
 * in turn boils down to setting the LogPipe->next member).
 *
 * The tree like structure is created using LogMultiplexer instances,
 * pipes are connected back with a simple LogPipe instance that only
 * forwards messages.
 *
 * The next member pointer is not holding a reference, but can be
 * assumed to be kept alive as long as the configuration is running.
 *
 * Parameters:
 * @self: the CfgTree instance
 * @rule: the series of LogExprNode instances encapsulates as a LogExprNode
 * @outer_pipe_tail: the last LogPipe to be used to chain further elements to this sequence
 * @cfg: GlobalConfig instance
 * @toplevel: whether this rule is a top-level one.
 * @flow_controlled_parent: specifies whether the parent log statement has flags(flow-controlled)
 **/
static gboolean
cfg_tree_compile_sequence(CfgTree *self, LogExprNode *node,
                          LogPipe **outer_pipe_head, LogPipe **outer_pipe_tail,
                          gboolean flow_controlled_parent)
{
  LogExprNode *ep;
  LogPipe
    *first_pipe,   /* the head of the constructed pipeline */
    *last_pipe;    /* the current tail of the constructed pipeline */
  LogPipe *source_join_pipe = NULL;
  gboolean  path_changes_the_message = FALSE, flow_controlled_child = FALSE;

  if ((node->flags & LC_CATCHALL) != 0)
    {
      /* the catch-all resolution code clears this flag */

      msg_error("Error in configuration, catch-all flag can only be specified for top-level log statements",
                NULL);
      goto error;
    }

  /* the loop below creates a sequence of LogPipe instances which
   * essentially execute the user configuration once it is
   * started.
   *
   * The input of this is a log expression, denoted by a tree of
   * LogExprNode structures, built by the parser. We are storing the
   * sequence as a linked list, pipes are linked with their "next"
   * field.
   *
   * The head of this list is pointed to by @first_pipe, the current
   * end is known as @last_pipe.
   *
   * In case the sequence starts with a source LogPipe (PIF_SOURCE
   * flag), the head of the list is _not_ tracked, in that case
   * first_pipe is NULL.
   *
   */

  first_pipe = last_pipe = NULL;

  for (ep = node->children; ep; ep = ep->next)
    {
      LogPipe *sub_pipe_head = NULL, *sub_pipe_tail = NULL;

      if (!cfg_tree_compile_node(self, ep, &sub_pipe_head, &sub_pipe_tail, flow_controlled_parent || (ep->flags & LC_FLOW_CONTROL)))
        goto error;

      /* add pipe to the current pipe_line, e.g. after last_pipe, update last_pipe & first_pipe */
      if (sub_pipe_head)
        {
          if (sub_pipe_head->flags & PIF_CLONE)
            path_changes_the_message = TRUE;

          if (sub_pipe_head->flags & PIF_HARD_FLOW_CONTROL)
            flow_controlled_child = TRUE;

          if (!first_pipe && !last_pipe)
            {
              /* we only remember the first pipe in case we're not in
               * source mode. In source mode, only last_pipe is set */

              first_pipe = sub_pipe_head;
            }

          if (last_pipe)
            {
              g_assert(last_pipe->pipe_next == NULL);
              log_pipe_append(last_pipe, sub_pipe_head);
            }

          if (sub_pipe_tail)
            {
              last_pipe = sub_pipe_tail;
            }
          else
            {
              last_pipe = sub_pipe_head;
              /* look for the final pipe */
              while (last_pipe->pipe_next)
                {
                  last_pipe = last_pipe->pipe_next;
                }
            }
          sub_pipe_head = NULL;
        }
      else if (sub_pipe_tail)
        {
          /* source pipe */

          if (first_pipe)
            {
              msg_error("Error compiling sequence, source-pipe follows a non-source one, please list source references/definitions first",
                        evt_tag_str("location", log_expr_node_format_location(ep)),
                        NULL);
              goto error;
            }

          if (!source_join_pipe)
            {
              source_join_pipe = last_pipe = log_pipe_new();
              g_ptr_array_add(self->initialized_pipes, source_join_pipe);
            }
          log_pipe_append(sub_pipe_tail, source_join_pipe);
        }
    }

  if (first_pipe)
    {
      if (node->flags & LC_FALLBACK)
        first_pipe->flags |= PIF_BRANCH_FALLBACK;

      if (node->flags & LC_FINAL)
        first_pipe->flags |= PIF_BRANCH_FINAL;

      if (path_changes_the_message)
        first_pipe->flags |= PIF_CLONE;

      if ((node->flags & LC_FLOW_CONTROL) || flow_controlled_child || flow_controlled_parent)
        first_pipe->flags |= PIF_HARD_FLOW_CONTROL;
    }

  *outer_pipe_tail = last_pipe;
  *outer_pipe_head = first_pipe;
  return TRUE;
 error:

  /* we don't need to free anything, everything we allocated is recorded in
   * @self, thus will be freed whenever cfg_tree_free is called */

  return FALSE;
}

/**
 * cfg_tree_compile_junction():
 *
 * This function builds a junction within the configuration. A
 * junction is where processing is forked into several branches, each
 * doing its own business, and then the end of each branch is
 * collected at the end so that further processing can be done on the
 * combined output of each log branch.
 *
 *       /-- branch --\
 *      /              \
 *  ---+---- branch ----+---
 *      \              /
 *       \-- branch --/
 **/
static gboolean
cfg_tree_compile_junction(CfgTree *self,
                          LogExprNode *node,
                          LogPipe **outer_pipe_head, LogPipe **outer_pipe_tail,
                          gboolean flow_controlled_parent)
{
  LogExprNode *ep;
  LogPipe *join_pipe = NULL;    /* the pipe where parallel branches are joined in a junction */
  LogMultiplexer *fork_mpx = NULL;
  gboolean flow_controlled_child = FALSE;

  for (ep = node->children; ep; ep = ep->next)
    {
      LogPipe *sub_pipe_head = NULL, *sub_pipe_tail = NULL;
      gboolean is_first_branch = (ep == node->children);

      if (!cfg_tree_compile_node(self, ep, &sub_pipe_head, &sub_pipe_tail, flow_controlled_parent || (ep->flags & LC_FLOW_CONTROL)))
        goto error;

      if (sub_pipe_head)
        {
          /* ep is an intermediate LogPipe or a destination, we have to fork */

          if (!is_first_branch && !fork_mpx)
            {
              msg_error("Error compiling junction, source and non-source branches are mixed",
                        evt_tag_str("location", log_expr_node_format_location(ep)),
                        NULL);
              goto error;
            }
          if (!fork_mpx)
            {
              fork_mpx = log_multiplexer_new(0);
              g_ptr_array_add(self->initialized_pipes, &fork_mpx->super);
            }
          log_multiplexer_add_next_hop(fork_mpx, sub_pipe_head);
          if (sub_pipe_head->flags & PIF_HARD_FLOW_CONTROL)
            flow_controlled_child = TRUE;
        }
      else
        {
          /* ep is a "source" LogPipe (cause no sub_pipe_head returned by compile_node). */

          if (fork_mpx)
            {
              msg_error("Error compiling junction, source and non-source branches are mixed",
                        evt_tag_str("location", log_expr_node_format_location(ep)),
                        NULL);
              goto error;
            }
        }

      if (sub_pipe_tail && outer_pipe_tail)
        {
          if (!join_pipe)
            {
              join_pipe = log_pipe_new();
              g_ptr_array_add(self->initialized_pipes, join_pipe);
            }
          log_pipe_append(sub_pipe_tail, join_pipe);

        }
    }

  if (fork_mpx && (flow_controlled_child || flow_controlled_parent))
    fork_mpx->super.flags |= PIF_HARD_FLOW_CONTROL;

  *outer_pipe_head = &fork_mpx->super;
  if (outer_pipe_tail)
    *outer_pipe_tail = join_pipe;
  return TRUE;
 error:

  /* we don't need to free anything, everything we allocated is recorded in
   * @self, thus will be freed whenever cfg_tree_free is called */

  return FALSE;
}

/*
 * cfg_tree_compile_node:
 *
 * This function takes care of compiling a LogExprNode.
 */
gboolean
cfg_tree_compile_node(CfgTree *self, LogExprNode *node,
                      LogPipe **outer_pipe_head, LogPipe **outer_pipe_tail,
                      gboolean flow_controlled_parent)
{
  static gint indent = -1;
  gboolean result = FALSE;

  indent++;
  fprintf(stderr, "%.*sCompiling %s %s [%s]\n",
          indent * 2, "                   ",
          node->name ? : "#unnamed",
          log_expr_node_get_layout_name(node->layout),
          log_expr_node_get_content_name(node->content));
  switch (node->layout)
    {
    case ENL_SINGLE:
      result = cfg_tree_compile_single(self, node, outer_pipe_head, outer_pipe_tail);
      break;
    case ENL_REFERENCE:
      result = cfg_tree_compile_reference(self, node, outer_pipe_head, outer_pipe_tail, flow_controlled_parent);
      break;
    case ENL_SEQUENCE:
      result = cfg_tree_compile_sequence(self, node, outer_pipe_head, outer_pipe_tail, flow_controlled_parent);
      break;
    case ENL_JUNCTION:
      result = cfg_tree_compile_junction(self, node, outer_pipe_head, outer_pipe_tail, flow_controlled_parent);
      break;
    default:
      g_assert_not_reached();
    }
  indent--;
  return result;
}

gboolean
cfg_tree_compile_rule(CfgTree *self, LogExprNode *rule)
{
  LogPipe *sub_pipe_head = NULL, *sub_pipe_tail = NULL;

  return cfg_tree_compile_node(self, rule, &sub_pipe_head, &sub_pipe_tail, FALSE);
}

static gboolean
cfg_tree_objects_equal(gconstpointer v1, gconstpointer v2)
{
  LogExprNode *r1 = (LogExprNode *) v1;
  LogExprNode *r2 = (LogExprNode *) v2;

  if (r1->content != r2->content)
    return FALSE;

  /* we assume that only rules with a name are hashed */

  return strcmp(r1->name, r2->name) == 0;
}

static guint
cfg_tree_objects_hash(gconstpointer v)
{
  LogExprNode *r = (LogExprNode *) v;

  /* we assume that only rules with a name are hashed */
  return r->content + g_str_hash(r->name);
}

gboolean
cfg_tree_add_object(CfgTree *self, LogExprNode *rule)
{
  gboolean res = TRUE;

  if (rule->name)
    {
      /* only named rules can be stored as objects to be referenced later */

      /* check if already present */
      res = (g_hash_table_lookup(self->objects, rule) == NULL);

      /* key is the same as the object */
      g_hash_table_replace(self->objects, rule, rule);
    }
  else
    {
      /* unnamed rules are simply put in the rules array */
      g_ptr_array_add(self->rules, rule);
    }

  return res;
}

LogExprNode *
cfg_tree_get_object(CfgTree *self, gint content, const gchar *name)
{
  LogExprNode lookup_node;

  memset(&lookup_node, 0, sizeof(lookup_node));
  lookup_node.content = content;
  lookup_node.name = (gchar *) name;

  return g_hash_table_lookup(self->objects, &lookup_node);
}

gboolean
cfg_tree_add_template(CfgTree *self, LogTemplate *template)
{
  gboolean res = TRUE;

  res = (g_hash_table_lookup(self->templates, template->name) == NULL);
  g_hash_table_replace(self->templates, template->name, template);
  return res;
}

LogTemplate *
cfg_tree_lookup_template(CfgTree *self, const gchar *name)
{
  if (name)
    return log_template_ref(g_hash_table_lookup(self->templates, name));
  return NULL;
}

LogTemplate *
cfg_tree_check_inline_template(CfgTree *self, const gchar *template_or_name, GError **error)
{
  LogTemplate *template = cfg_tree_lookup_template(self, template_or_name);

  if (template == NULL)
    {
      template = log_template_new(self->cfg, NULL);
      log_template_compile(template, template_or_name, error);
      template->def_inline = TRUE;
    }
  return template;
}

gboolean
cfg_tree_compile(CfgTree *self)
{
  gint i;

  /* resolve references within the configuration */

  for (i = 0; i < self->rules->len; i++)
    {
      LogExprNode *rule = (LogExprNode *) g_ptr_array_index(self->rules, i);

      if ((rule->flags & LC_CATCHALL))
        {
          gpointer args[] = { self, rule };

          g_hash_table_foreach(self->objects, cfg_tree_add_all_sources, args);
          rule->flags &= ~LC_CATCHALL;
        }

      if (!cfg_tree_compile_rule(self, rule))
        {
          return FALSE;
        }
    }
  return TRUE;
}

gboolean
cfg_tree_start(CfgTree *self)
{
  gint i;

  if (!cfg_tree_compile(self))
    return FALSE;

  /*
   *   As there are pipes that are dynamically created during init, these
   *   pipes must be deinited before destroying the configuration, otherwise
   *   circular references will inhibit the free of the configuration
   *   structure.
   */
  for (i = 0; i < self->initialized_pipes->len; i++)
    {
      if (!log_pipe_init(g_ptr_array_index(self->initialized_pipes, i), self->cfg))
        {
          msg_error("Error initializing message pipeline",
                    NULL);
          return FALSE;
        }
    }
#if 0
 stats_lock();
  stats_register_counter(0, SCS_CENTER, NULL, "received", SC_TYPE_PROCESSED, &self->received_messages);
  stats_register_counter(0, SCS_CENTER, NULL, "queued", SC_TYPE_PROCESSED, &self->queued_messages);
  stats_unlock();
#endif
  return TRUE;
}

gboolean
cfg_tree_stop(CfgTree *self)
{
  gboolean success = TRUE;
  gint i;

  for (i = 0; i < self->initialized_pipes->len; i++)
    {
      if (!log_pipe_deinit(g_ptr_array_index(self->initialized_pipes, i)))
        success = FALSE;
    }

#if 0
  stats_lock();
  stats_unregister_counter(SCS_CENTER, NULL, "received", SC_TYPE_PROCESSED, &self->received_messages);
  stats_unregister_counter(SCS_CENTER, NULL, "queued", SC_TYPE_PROCESSED, &self->queued_messages);
  stats_unlock();
#endif
  return success;
}

void
cfg_tree_init_instance(CfgTree *self, GlobalConfig *cfg)
{
  self->initialized_pipes = g_ptr_array_new();
  self->objects = g_hash_table_new_full(cfg_tree_objects_hash, cfg_tree_objects_equal, NULL, (GDestroyNotify) log_expr_node_free);
  self->templates = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify) log_template_unref);
  self->rules = g_ptr_array_new();
  self->cfg = cfg;
}

void
cfg_tree_free_instance(CfgTree *self)
{
  g_ptr_array_foreach(self->initialized_pipes, (GFunc) log_pipe_unref, NULL);
  g_ptr_array_free(self->initialized_pipes, TRUE);

  g_ptr_array_foreach(self->rules, (GFunc) log_expr_node_free, NULL);
  g_ptr_array_free(self->rules, TRUE);

  g_hash_table_destroy(self->templates);
  self->cfg = NULL;
}
