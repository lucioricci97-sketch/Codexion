/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   codexion.h                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: luricci <luricci@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/22 16:15:52 by luricci           #+#    #+#             */
/*   Updated: 2026/07/02 16:17:28 by luricci          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CODEXION_H
# define CODEXION_H

# include <pthread.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <sys/time.h>
# include <time.h>
# include <unistd.h>

# define OK   0
# define ERR  1
# define FIFO 0
# define EDF  1

typedef struct s_args
{
	int	n_coders;
	int	t_burnout;
	int	t_compile;
	int	t_debug;
	int	t_refactor;
	int	n_required;
	int	t_cooldown;
	int	scheduler;
}	t_args;

typedef struct s_request
{
	int		id;
	long	key;
}	t_request;

typedef struct s_heap
{
	t_request	items[2];
	int			size;
}	t_heap;

typedef struct s_sim		t_sim;
typedef struct s_coder		t_coder;
typedef struct s_dongle		t_dongle;

struct s_dongle
{
	int				id;
	int				available;
	long			last_release;
	t_heap			queue;
	pthread_mutex_t	mtx;
	pthread_cond_t	cond;
};

struct s_coder
{
	int				id;
	int				compiles;
	long			last_compile;
	pthread_mutex_t	mtx;
	t_dongle		*low;
	t_dongle		*high;
	t_sim			*sim;
};

struct s_sim
{
	t_args			args;
	long			start;
	int				stop;
	int				ready;
	int				go;
	int				started;
	int				mon_started;
	t_coder			*coders;
	t_dongle		*dongles;
	pthread_t		*threads;
	pthread_t		mon;
	pthread_mutex_t	print_mtx;
	pthread_mutex_t	sim_mtx;
	pthread_cond_t	sim_cond;
};

int		parse_args(int ac, char **av, t_args *args);

int		sim_init(t_sim *sim);
void	sim_destroy(t_sim *sim);

void	heap_push(t_heap *h, int id, long key);
int		heap_top(t_heap *h);
void	heap_pop(t_heap *h);

int		acquire_dongles(t_coder *c);
void	release_dongles(t_coder *c);

void	*coder_routine(void *arg);

void	*monitor_routine(void *arg);
void	wake_all(t_sim *sim);

long	now_us(void);
int		log_state(t_coder *c, const char *msg);
void	force_log(t_coder *c, const char *msg);
int		sim_stopped(t_sim *sim);
int		precise_sleep(t_coder *c, long us);

#endif
