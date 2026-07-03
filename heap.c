/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   heap.c                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: luricci <luricci@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/22 16:18:17 by luricci           #+#    #+#             */
/*   Updated: 2026/07/02 16:18:31 by luricci          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */



#include "codexion.h"

static void	swap_req(t_request *a, t_request *b)
{
	t_request	tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

static int	req_less(const t_request *a, const t_request *b)
{
	if (a->key != b->key)
		return (a->key < b->key);
	return (a->id < b->id);
}

static void	bubble_up(t_heap *h, int i)
{
	int	parent;

	while (i > 0)
	{
		parent = (i - 1) / 2;
		if (req_less(&h->items[i], &h->items[parent]))
		{
			swap_req(&h->items[i], &h->items[parent]);
			i = parent;
		}
		else
			break ;
	}
}

static void	sift_down(t_heap *h, int i)
{
	int	left;
	int	right;
	int	smallest;

	while (1)
	{
		left = i * 2 + 1;
		right = i * 2 + 2;
		smallest = i;
		if (left < h->size && req_less(&h->items[left], &h->items[smallest]))
			smallest = left;
		if (right < h->size && req_less(&h->items[right], &h->items[smallest]))
			smallest = right;
		if (smallest == i)
			break ;
		swap_req(&h->items[smallest], &h->items[i]);
		i = smallest;
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

int	heap_top(t_heap *h)
{
	if (h->size == 0)
		return (-1);
	return (h->items[0].id);
}

void	heap_pop(t_heap *h)
{
	if (h->size == 0)
		return ;
	h->size--;
	if (h->size > 0)
	{
		swap_req(&h->items[0], &h->items[h->size]);
		sift_down(h, 0);
	}
}
