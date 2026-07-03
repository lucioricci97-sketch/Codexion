/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   heap.c                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: luricci <luricci@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/22 16:18:17 by luricci           #+#    #+#             */
/*   Updated: 2026/07/03 17:50:59 by luricci          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

static int	req_less(const t_request *a, const t_request *b)
{
	if (a->key != b->key)
		return (a->key < b->key);
	return (a->id < b->id);
}

static void	bubble_up(t_heap *h, int i)
{
	int			parent;
	t_request	tmp;

	while (i > 0)
	{
		parent = (i - 1) / 2;
		if (req_less(&h->items[i], &h->items[parent]))
		{
			tmp = h->items[i];
			h->items[i] = h->items[parent];
			h->items[parent] = tmp;
			i = parent;
		}
		else
			break ;
	}
}

static void	sift_down(t_heap *h, int i)
{
	int			l;
	int			r;
	int			s;
	t_request	tmp;

	while (1)
	{
		l = i * 2 + 1;
		r = i * 2 + 2;
		s = i;
		if (l < h->size && req_less(&h->items[l], &h->items[s]))
			s = l;
		if (r < h->size && req_less(&h->items[r], &h->items[s]))
			s = r;
		if (s == i)
			break ;
		tmp = h->items[s];
		h->items[s] = h->items[i];
		h->items[i] = tmp;
		i = s;
	}
}

void	heap_push(t_heap *h, int id, long key)
{
	if (h->size >= 2)
		return ;
	h->items[h->size].id = id;
	h->items[h->size].key = key;
	bubble_up(h, h->size);
	h->size++;
}

void	heap_pop(t_heap *h)
{
	t_request	tmp;

	if (h->size == 0)
		return ;
	h->size--;
	if (h->size > 0)
	{
		tmp = h->items[0];
		h->items[0] = h->items[h->size];
		h->items[h->size] = tmp;
		sift_down(h, 0);
	}
}
