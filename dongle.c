/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   dongle.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: luricci <luricci@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/22 16:17:50 by luricci           #+#    #+#             */
/*   Updated: 2026/07/02 16:18:05 by luricci          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */


#include "codexion.h"

static long	pick_key(t_coder *c)
{
	long	last;

	if (c->sim->args.scheduler == EDF)
	{
		pthread_mutex_lock(&c->mtx);
		last = c->last_compile;
		pthread_mutex_unlock(&c->mtx);
		return (last + (long)c->sim->args.t_burnout * 1000L);
	}
	return (now_us());
}

static void	register_request(t_dongle *d, int id, long key)
{
	pthread_mutex_lock(&d->mtx);
	heap_push(&d->queue, id, key);
	pthread_cond_broadcast(&d->cond);
	pthread_mutex_unlock(&d->mtx);
}

static int	try_take(t_dongle *d, t_coder *c)
{
	long	wait_until;

	wait_until = d->last_release + (long)c->sim->args.t_cooldown * 1000L;
	if (heap_top(&d->queue) != c->id)
		return (0);
	if (!d->available)
		return (0);
	if (now_us() < wait_until)
		return (0);
	heap_pop(&d->queue);
	d->available = 0;
	return (1);
}

static void	compute_abs(struct timespec *ts, long delta_us)
{
	struct timeval	tv;
	long			total_us;

	gettimeofday(&tv, NULL);
	total_us = tv.tv_usec + delta_us;
	ts->tv_sec = tv.tv_sec + total_us / 1000000L;
	ts->tv_nsec = (total_us % 1000000L) * 1000L;
}

static void	wait_choice(t_dongle *d, t_coder *c)
{
	long			now;
	long			wait_until;
	struct timespec	ts;

	now = now_us();
	wait_until = d->last_release + (long)c->sim->args.t_cooldown * 1000L;
	if (d->available && heap_top(&d->queue) == c->id && now < wait_until)
	{
		compute_abs(&ts, wait_until - now);
		pthread_cond_timedwait(&d->cond, &d->mtx, &ts);
	}
	else
		pthread_cond_wait(&d->cond, &d->mtx);
}

static int	wait_for_dongle(t_dongle *d, t_coder *c)
{
	pthread_mutex_lock(&d->mtx);
	while (1)
	{
		if (sim_stopped(c->sim))
		{
			pthread_mutex_unlock(&d->mtx);
			return (ERR);
		}
		if (try_take(d, c))
		{
			pthread_mutex_unlock(&d->mtx);
			return (OK);
		}
		wait_choice(d, c);
	}
}

static int	release_one(t_dongle *d)
{
	pthread_mutex_lock(&d->mtx);
	d->available = 1;
	d->last_release = now_us();
	pthread_cond_broadcast(&d->cond);
	pthread_mutex_unlock(&d->mtx);
	return (ERR);
}

int	acquire_dongles(t_coder *c)
{
	long	key;

	key = pick_key(c);
	register_request(c->low, c->id, key);
	if (wait_for_dongle(c->low, c))
		return (ERR);
	if (log_state(c, "has taken a dongle"))
		return (release_one(c->low));
	key = pick_key(c);
	register_request(c->high, c->id, key);
	if (wait_for_dongle(c->high, c))
		return (release_one(c->low));
	if (log_state(c, "has taken a dongle"))
	{
		release_one(c->low);
		return (release_one(c->high));
	}
	return (OK);
}

void	release_dongles(t_coder *c)
{
	release_one(c->low);
	release_one(c->high);
}
