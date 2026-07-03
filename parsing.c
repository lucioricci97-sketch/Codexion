/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   parsing.c                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: luricci <luricci@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/22 16:19:44 by luricci           #+#    #+#             */
/*   Updated: 2026/07/02 16:19:51 by luricci          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */



#include "codexion.h"

static int	ft_atoi_pos(const char *s, int *out)
{
	long	n;

	if (!*s)
		return (ERR);
	n = 0;
	while (*s)
	{
		if (*s < '0' || *s > '9')
			return (ERR);
		n = n * 10 + (*s - '0');
		if (n > 2147483647L)
			return (ERR);
		s++;
	}
	*out = (int)n;
	return (OK);
}

static int	parse_scheduler(const char *s, int *out)
{
	if (!strcmp(s, "fifo"))
		*out = FIFO;
	else if (!strcmp(s, "edf"))
		*out = EDF;
	else
		return (ERR);
	return (OK);
}

static int	parse_numbers(char **av, t_args *a)
{
	int	*fields[7];
	int	i;

	fields[0] = &a->n_coders;
	fields[1] = &a->t_burnout;
	fields[2] = &a->t_compile;
	fields[3] = &a->t_debug;
	fields[4] = &a->t_refactor;
	fields[5] = &a->n_required;
	fields[6] = &a->t_cooldown;
	i = 0;
	while (i < 7)
	{
		if (ft_atoi_pos(av[i + 1], fields[i]))
			return (ERR);
		i++;
	}
	return (OK);
}

int	parse_args(int ac, char **av, t_args *a)
{
	if (ac != 9)
		return (ERR);
	if (parse_numbers(av, a))
		return (ERR);
	if (parse_scheduler(av[8], &a->scheduler))
		return (ERR);
	if (a->n_coders < 1 || a->n_required < 1)
		return (ERR);
	return (OK);
}
