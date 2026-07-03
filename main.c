/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.c                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: luricci <luricci@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/22 16:19:03 by luricci           #+#    #+#             */
/*   Updated: 2026/07/02 16:22:14 by luricci          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */



#include "codexion.h"

static int	spawn_threads(t_sim *sim)
{
	int	i;

	i = 0;
	while (i < sim->args.n_coders)
	{
		if (pthread_create(&sim->threads[i], NULL, coder_routine,
				&sim->coders[i]))
			return (ERR);
		sim->started++;
		i++;
	}
	if (pthread_create(&sim->mon, NULL, monitor_routine, sim))
		return (ERR);
	sim->mon_started = 1;
	return (OK);
}

static void	stamp_starts(t_sim *sim)
{
	int	i;

	i = 0;
	while (i < sim->args.n_coders)
	{
		pthread_mutex_lock(&sim->coders[i].mtx);
		sim->coders[i].last_compile = sim->start;
		pthread_mutex_unlock(&sim->coders[i].mtx);
		i++;
	}
}

static void	start_simulation(t_sim *sim)
{
	while (1)
	{
		pthread_mutex_lock(&sim->sim_mtx);
		if (sim->ready >= sim->args.n_coders)
			break ;
		pthread_mutex_unlock(&sim->sim_mtx);
		usleep(500);
	}
	sim->start = now_us();
	stamp_starts(sim);
	sim->go = 1;
	pthread_cond_broadcast(&sim->sim_cond);
	pthread_mutex_unlock(&sim->sim_mtx);
}

static void	join_all(t_sim *sim)
{
	int	i;

	i = 0;
	while (i < sim->started)
	{
		pthread_join(sim->threads[i], NULL);
		i++;
	}
	if (sim->mon_started)
		pthread_join(sim->mon, NULL);
}

static void	abort_threads(t_sim *sim)
{
	pthread_mutex_lock(&sim->sim_mtx);
	sim->stop = 1;
	sim->go = 1;
	pthread_cond_broadcast(&sim->sim_cond);
	pthread_mutex_unlock(&sim->sim_mtx);
	wake_all(sim);
}

static int	run(t_sim *sim)
{
	if (spawn_threads(sim))
	{
		abort_threads(sim);
		join_all(sim);
		return (ERR);
	}
	start_simulation(sim);
	join_all(sim);
	return (OK);
}

int	main(int ac, char **av)
{
	t_sim	sim;
	int		status;

	memset(&sim, 0, sizeof(t_sim));
	if (parse_args(ac, av, &sim.args))
	{
		fprintf(stderr, "Usage: %s n_coders t_burnout t_compile t_debug "
			"t_refactor n_required t_cooldown {fifo|edf}\n", av[0]);
		return (1);
	}
	if (sim_init(&sim))
	{
		sim_destroy(&sim);
		return (1);
	}
	status = run(&sim);
	sim_destroy(&sim);
	return (status);
}
