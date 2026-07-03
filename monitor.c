/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   monitor.c                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: luricci <luricci@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/22 16:19:28 by luricci           #+#    #+#             */
/*   Updated: 2026/07/02 16:19:36 by luricci          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */


#include "codexion.h"

void	wake_all(t_sim *sim)
{
	int	i;
	int	n;

	n = sim->args.n_coders;
	pthread_mutex_lock(&sim->sim_mtx);
	pthread_cond_broadcast(&sim->sim_cond);
	pthread_mutex_unlock(&sim->sim_mtx);
	i = 0;
	while (i < n)
	{
		pthread_mutex_lock(&sim->dongles[i].mtx);
		pthread_cond_broadcast(&sim->dongles[i].cond);
		pthread_mutex_unlock(&sim->dongles[i].mtx);
		i++;
	}
}

static void	mark_stop(t_sim *sim)
{
	pthread_mutex_lock(&sim->sim_mtx);
	sim->stop = 1;
	pthread_mutex_unlock(&sim->sim_mtx);
}

static int	check_burnout(t_sim *sim, int i)
{
	long	last;
	long	deadline;

	pthread_mutex_lock(&sim->coders[i].mtx);
	last = sim->coders[i].last_compile;
	pthread_mutex_unlock(&sim->coders[i].mtx);
	deadline = last + (long)sim->args.t_burnout * 1000L;
	if (now_us() >= deadline)
	{
		mark_stop(sim);
		force_log(&sim->coders[i], "burned out");
		wake_all(sim);
		return (1);
	}
	return (0);
}

static int	check_all_done(t_sim *sim)
{
	int	i;
	int	done;

	done = 1;
	i = 0;
	while (i < sim->args.n_coders)
	{
		pthread_mutex_lock(&sim->coders[i].mtx);
		if (sim->coders[i].compiles < sim->args.n_required)
			done = 0;
		pthread_mutex_unlock(&sim->coders[i].mtx);
		i++;
	}
	if (done)
	{
		mark_stop(sim);
		wake_all(sim);
	}
	return (done);
}

static void	monitor_wait_start(t_sim *sim)
{
	pthread_mutex_lock(&sim->sim_mtx);
	while (!sim->go)
		pthread_cond_wait(&sim->sim_cond, &sim->sim_mtx);
	pthread_mutex_unlock(&sim->sim_mtx);
}

void	*monitor_routine(void *arg)
{
	t_sim	*sim;
	int		i;

	sim = (t_sim *)arg;
	monitor_wait_start(sim);
	while (!sim_stopped(sim))
	{
		i = 0;
		while (i < sim->args.n_coders)
		{
			if (check_burnout(sim, i))
				return (NULL);
			i++;
		}
		if (check_all_done(sim))
			return (NULL);
		usleep(500);
	}
	return (NULL);
}
