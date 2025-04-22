NAME = ircserv

SRC = server/server.cpp client/client.cpp channels/channels.cpp main.cpp

CFLAGS = -Wall -Wextra -Werror -std=c++98

CC = c++

HEADER = server/server.hpp client/client.hpp channels/channels.hpp

OBJ=$(SRC:.cpp=.o)

all : $(NAME)

$(NAME) : $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(NAME)

%.o : %.cpp $(HEADER)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean :
	rm -rf $(OBJ)

fclean : clean
	rm -rf $(NAME)

re : fclean all