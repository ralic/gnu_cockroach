/*
   libroach core.

   Copyright (C) 2011 Jose E. Marchesi <jemarch@gnu.org>
   Copyright (C) 2011 Giuseppe Scrivano <gscrivano@gnu.org>

   cockroach is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   GNU Wget is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "roach.h"
#include <stdlib.h>
#include <sys/ptrace.h>

roach_hook_t *
roach_make_hook (enum HOOK_TYPE type, int *syscalls, hook_func_t hook,
                 void *data)
{
  int max_sc = 0, *syscall;
  roach_hook_t *r = malloc (sizeof *r);
  if (r == NULL)
    return r;

  r->next = NULL;
  r->type = type;
  r->hook = hook;
  r->data = data;

  for (syscall = syscalls; *syscall; syscall++)
    if (*syscall > max_sc)
      max_sc = *syscall;

  r->syscalls = roach_bitmap_make (max_sc);
  if (r->syscalls == NULL)
    {
      free (r);
      return NULL;
    }

  for (syscall = syscalls; *syscall; syscall++)
    roach_bitmap_set (r->syscalls, *syscall);

  return r;
}

void
roach_free_hook (roach_hook_t *ctx)
{
  roach_bitmap_free (ctx->syscalls);
  free (ctx);
}


roach_context_t *
roach_make_context ()
{
  roach_context_t *c = malloc (sizeof *c);
  if (c == NULL)
    return c;

  c->hooks = NULL;
  c->entering_sc = false;
  c->last_syscall = 0;

  return c;
}

void
roach_free_context (roach_context_t *ctx)
{
  free (ctx);
}

void
roach_ctx_add_hook (roach_context_t *ctx, roach_hook_t *hook)
{
  hook->next = NULL;

  if (ctx->hooks == NULL)
    ctx->hooks = hook;
  else
    {
      roach_hook_t *it = ctx->hooks;

      while (it->next)
        it = it->next;

      it->next = hook;
    }
}

void
roach_ctx_rm_hook (roach_context_t *ctx, roach_hook_t *hook)
{
  roach_hook_t *prev = NULL, *it = ctx->hooks;
  while (it)
    {
      if (it == hook)
        break;

      prev = it;
      it = it->next;
    }

  if (! it)
    return;

  if (prev)
    prev->next = it->next;
  else
    ctx->hooks = it->next;
}

int
roach_spawn_process (roach_context_t *ctx, char const *exec, char *const *argv)
{
  pid_t pid = fork ();
  if (pid == 0)
    {
      if (ptrace (PTRACE_TRACEME, 0, NULL, NULL) < 0)
        exit (EXIT_FAILURE);
      execv (exec, argv);
      exit (EXIT_FAILURE);
    }
  wait ();
  ctx->pid = pid;
  return pid;
}

int
roach_wait (roach_context_t *ctx, int *status)
{
  int ret, syscall;
  roach_hook_t *hook;

  ptrace (PTRACE_SYSCALL, ctx->pid, NULL, NULL);
  ret = waitpid (ctx->pid, status, NULL);

  if (ret > 0)
    ctx->entering_sc = !ctx->entering_sc;

  if (ctx->entering_sc)
    ctx->last_syscall = syscall = roach_get_sc (ctx);
  else
    syscall = ctx->last_syscall;

  for (hook = ctx->hooks; hook; hook = hook->next)
    {
      if (!ctx->entering_sc && hook->type == HOOK_ENTER)
        continue;

      if (ctx->entering_sc && hook->type == HOOK_EXIT)
        continue;

      /* What to do with the return code?  */
      if (roach_bitmap_p (hook->syscalls, syscall))
        hook->hook (ctx, ctx->entering_sc, hook->data);
    }

  return ret;
}

bool
roach_entering_sc_p (roach_context_t *ctx)
{
  return ctx->entering_sc;
}

int
roach_reg_syscall (roach_context_t *ctx, int syscall, hook_func_t hook_func,
                   void *data)
{
  roach_hook_t *hook;
  int syscalls[] = {syscall, 0};

  hook = roach_make_hook (HOOK_BOTH, syscalls, hook_func, -1);
  if (hook == NULL)
    return -1;

  roach_ctx_add_hook (ctx, hook);
  return 0;
}