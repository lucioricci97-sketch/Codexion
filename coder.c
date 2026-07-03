/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   coder.c                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: luricci <luricci@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/22 16:15:18 by luricci           #+#    #+#             */
/*   Updated: 2026/07/03 17:51:18 by luricci          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

static int	do_compile(t_coder *c)
{
	pthread_mutex_lock(&c->mtx);
	c->last_compile = now_us();
	pthread_mutex_unlock(&c->mtx);
	if (log_state(c, "is compiling"))
	{
		release_one(c->low);
		release_one(c->high);
		return (ERR);
	}
	if (precise_sleep(c, (long)c->sim->args.t_compile * 1000L))
	{
		release_one(c->low);
		release_one(c->high);
		return (ERR);
	}
	pthread_mutex_lock(&c->mtx);
	c->compiles++;
	pthread_mutex_unlock(&c->mtx);
	release_one(c->low);
	release_one(c->high);
	return (OK);
}

static void	wait_for_start(t_coder *c)
{
	pthread_mutex_lock(&c->sim->sim_mtx);
	c->sim->ready++;
	while (!c->sim->go)
		pthread_cond_wait(&c->sim->sim_cond, &c->sim->sim_mtx);
	pthread_mutex_unlock(&c->sim->sim_mtx);
}

static int	stagger_start(t_coder *c)
{
	if (c->sim->args.n_coders <= 1)
		return (OK);
	if (c->id % 2 == 0 || c->id == c->sim->args.n_coders)
		return (precise_sleep(c, (long)c->sim->args.t_compile * 500L));
	return (OK);
}

void	*coder_routine(void *arg)
{
	t_coder	*c;

	c = (t_coder *)arg;
	wait_for_start(c);
	if (stagger_start(c))
		return (NULL);
	while (!sim_stopped(c->sim))
	{
		if (acquire_dongles(c))
			return (NULL);
		if (do_compile(c))
			return (NULL);
		if (log_state(c, "is debugging")
			|| precise_sleep(c, (long)c->sim->args.t_debug * 1000L))
			return (NULL);
		if (log_state(c, "is refactoring")
			|| precise_sleep(c, (long)c->sim->args.t_refactor * 1000L))
			return (NULL);
	}
	return (NULL);
}
