/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   init.c                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: luricci <luricci@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/22 16:18:48 by luricci           #+#    #+#             */
/*   Updated: 2026/07/03 17:49:59 by luricci          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

static int	init_dongles(t_sim *sim)
{
	int	i;
	int	n;

	n = sim->args.n_coders;
	i = 0;
	sim->dongles = (t_dongle *)malloc(sizeof(t_dongle) * n);
	if (!sim->dongles)
		return (ERR);
	memset(sim->dongles, 0, sizeof(t_dongle) * n);
	while (i < n)
	{
		sim->dongles[i].id = i;
		sim->dongles[i].available = 1;
		pthread_mutex_init(&sim->dongles[i].mtx, NULL);
		pthread_cond_init(&sim->dongles[i].cond, NULL);
		i++;
	}
	return (OK);
}

static void	assign_neighbors(t_sim *sim, int i)
{
	int	left;
	int	right;

	left = i;
	right = (i + 1) % sim->args.n_coders;
	if (left < right)
	{
		sim->coders[i].low = &sim->dongles[left];
		sim->coders[i].high = &sim->dongles[right];
	}
	else
	{
		sim->coders[i].low = &sim->dongles[right];
		sim->coders[i].high = &sim->dongles[left];
	}
}

static int	init_coders(t_sim *sim)
{
	int	i;
	int	n;

	n = sim->args.n_coders;
	i = 0;
	sim->coders = (t_coder *)malloc(sizeof(t_coder) * n);
	if (!sim->coders)
		return (ERR);
	memset(sim->coders, 0, sizeof(t_coder) * n);
	while (i < n)
	{
		sim->coders[i].id = i + 1;
		sim->coders[i].sim = sim;
		pthread_mutex_init(&sim->coders[i].mtx, NULL);
		assign_neighbors(sim, i);
		i++;
	}
	return (OK);
}

int	sim_init(t_sim *sim)
{
	pthread_mutex_init(&sim->print_mtx, NULL);
	pthread_mutex_init(&sim->sim_mtx, NULL);
	pthread_cond_init(&sim->sim_cond, NULL);
	if (init_dongles(sim))
		return (ERR);
	if (init_coders(sim))
		return (ERR);
	sim->threads = malloc(sizeof(pthread_t) * sim->args.n_coders);
	if (!sim->threads)
		return (ERR);
	return (OK);
}

void	sim_destroy(t_sim *sim)
{
	int	i;

	i = 0;
	while (sim->dongles && i < sim->args.n_coders)
	{
		pthread_mutex_destroy(&sim->dongles[i].mtx);
		pthread_cond_destroy(&sim->dongles[i].cond);
		i++;
	}
	i = 0;
	while (sim->coders && i < sim->args.n_coders)
	{
		pthread_mutex_destroy(&sim->coders[i].mtx);
		i++;
	}
	pthread_mutex_destroy(&sim->print_mtx);
	pthread_mutex_destroy(&sim->sim_mtx);
	pthread_cond_destroy(&sim->sim_cond);
	free(sim->dongles);
	free(sim->coders);
	free(sim->threads);
}
