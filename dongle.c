/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   dongle.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: luricci <luricci@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/22 16:17:50 by luricci           #+#    #+#             */
/*   Updated: 2026/07/03 17:50:54 by luricci          ###   ########.fr       */
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

static void	wait_choice(t_dongle *d, t_coder *c)
{
	long			now;
	long			wait_until;
	struct timespec	ts;
	struct timeval	tv;
	long			total_us;

	now = now_us();
	wait_until = d->last_release + (long)c->sim->args.t_cooldown * 1000L;
	if (d->available && d->queue.size > 0 && d->queue.items[0].id == c->id
		&& now < wait_until)
	{
		gettimeofday(&tv, NULL);
		total_us = tv.tv_usec + (wait_until - now);
		ts.tv_sec = tv.tv_sec + total_us / 1000000L;
		ts.tv_nsec = (total_us % 1000000L) * 1000L;
		pthread_cond_timedwait(&d->cond, &d->mtx, &ts);
	}
	else
		pthread_cond_wait(&d->cond, &d->mtx);
}

static int	wait_for_dongle(t_dongle *d, t_coder *c)
{
	long	wu;

	pthread_mutex_lock(&d->mtx);
	while (1)
	{
		if (sim_stopped(c->sim))
		{
			pthread_mutex_unlock(&d->mtx);
			return (ERR);
		}
		wu = d->last_release + (long)c->sim->args.t_cooldown * 1000L;
		if (d->available && d->queue.size > 0 && d->queue.items[0].id == c->id
			&& now_us() >= wu)
		{
			heap_pop(&d->queue);
			d->available = 0;
			pthread_mutex_unlock(&d->mtx);
			return (OK);
		}
		wait_choice(d, c);
	}
	return (ERR);
}

int	release_one(t_dongle *d)
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
	pthread_mutex_lock(&c->low->mtx);
	heap_push(&c->low->queue, c->id, key);
	pthread_cond_broadcast(&c->low->cond);
	pthread_mutex_unlock(&c->low->mtx);
	if (wait_for_dongle(c->low, c))
		return (ERR);
	if (log_state(c, "has taken a dongle"))
		return (release_one(c->low));
	key = pick_key(c);
	pthread_mutex_lock(&c->high->mtx);
	heap_push(&c->high->queue, c->id, key);
	pthread_cond_broadcast(&c->high->cond);
	pthread_mutex_unlock(&c->high->mtx);
	if (wait_for_dongle(c->high, c))
		return (release_one(c->low));
	if (log_state(c, "has taken a dongle"))
	{
		release_one(c->low);
		return (release_one(c->high));
	}
	return (OK);
}
