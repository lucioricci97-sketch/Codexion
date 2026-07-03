# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: luricci <luricci@student.42.fr>            +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2026/06/22 16:20:21 by luricci           #+#    #+#              #
#    Updated: 2026/07/02 16:20:31 by luricci          ###   ########.fr        #
#                                                                              #
# **************************************************************************** #


NAME	= codexion

SRCS	= main.c parsing.c init.c heap.c dongle.c coder.c monitor.c utils.c
OBJS	= $(SRCS:.c=.o)
HEADER	= codexion.h

CC		= cc
CFLAGS	= -Wall -Wextra -Werror -pthread

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(NAME)

%.o: %.c $(HEADER)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
