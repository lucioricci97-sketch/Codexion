/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   utils.c                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: luricci <luricci@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/22 16:20:05 by luricci           #+#    #+#             */
/*   Updated: 2026/07/03 17:50:34 by luricci          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

long	now_us(void)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);
	return ((long)tv.tv_sec * 1000000L + tv.tv_usec);
}

int	sim_stopped(t_sim *sim)
{
	int	s;

	pthread_mutex_lock(&sim->sim_mtx);
	s = sim->stop;
	pthread_mutex_unlock(&sim->sim_mtx);
	return (s);
}

int	log_state(t_coder *c, const char *msg)
{
	long	ts;
	long	stopped;

	pthread_mutex_lock(&c->sim->print_mtx);
	pthread_mutex_lock(&c->sim->sim_mtx);
	stopped = c->sim->stop;
	pthread_mutex_unlock(&c->sim->sim_mtx);
	if (stopped)
	{
		pthread_mutex_unlock(&c->sim->print_mtx);
		return (ERR);
	}
	ts = (now_us() - c->sim->start) / 1000L;
	printf("%ld %d %s\n", ts, c->id, msg);
	pthread_mutex_unlock(&c->sim->print_mtx);
	return (OK);
}

void	force_log(t_coder *c, const char *msg)
{
	long	ts;

	pthread_mutex_lock(&c->sim->print_mtx);
	ts = (now_us() - c->sim->start) / 1000L;
	printf("%ld %d %s\n", ts, c->id, msg);
	pthread_mutex_unlock(&c->sim->print_mtx);
}

int	precise_sleep(t_coder *c, long us)
{
	long	start;

	start = now_us();
	while (1)
	{
		if (sim_stopped(c->sim))
			return (ERR);
		if (now_us() - start >= us)
			return (OK);
		usleep(500);
	}
}
