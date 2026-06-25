# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: zsalih <zsalih@student.42abudhabi.ae>      +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2025/11/22 00:00:00 by webserv           #+#    #+#              #
#    Updated: 2026/06/25 09:47:17 by zsalih           ###   ########.fr        #
#                                                                              #
# **************************************************************************** #

NAME		= webserv

CXX			= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98 -I./include
RM			= rm -f

# Directories
SRC_DIR		= src
INC_DIR		= include
OBJ_DIR		= obj

# Source files
SRCS		= $(SRC_DIR)/main.cpp \
			  $(SRC_DIR)/Server.cpp \
			  $(SRC_DIR)/ServerConfig.cpp \
			  $(SRC_DIR)/Route.cpp \
			  $(SRC_DIR)/HttpRequest.cpp \
			  $(SRC_DIR)/HttpResponse.cpp \
			  $(SRC_DIR)/Socket.cpp \
			  $(SRC_DIR)/Client.cpp \
			  $(SRC_DIR)/CgiHandler.cpp \
			  $(SRC_DIR)/ConfigParser.cpp \
			  $(SRC_DIR)/RequestHandler.cpp \
			  $(SRC_DIR)/Logger.cpp \
			  $(SRC_DIR)/SessionMiddleware.cpp \
			  $(SRC_DIR)/CookieParser.cpp \
			  $(SRC_DIR)/SessionManager.cpp \
			  $(SRC_DIR)/FileRegistry.cpp

# Object files
OBJS		= $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# Header files
HEADERS		= $(INC_DIR)/Server.hpp \
			  $(INC_DIR)/ServerConfig.hpp \
			  $(INC_DIR)/Route.hpp \
			  $(INC_DIR)/HttpRequest.hpp \
			  $(INC_DIR)/HttpResponse.hpp \
			  $(INC_DIR)/Socket.hpp \
			  $(INC_DIR)/Client.hpp \
			  $(INC_DIR)/CgiHandler.hpp \
			  $(INC_DIR)/ConfigParser.hpp \
			  $(INC_DIR)/RequestHandler.hpp \
			  $(INC_DIR)/Logger.hpp \
			  $(INC_DIR)/SessionMiddleware.hpp \
			  $(INC_DIR)/CookieParser.hpp \
			  $(INC_DIR)/SessionManager.hpp \
			  $(INC_DIR)/FileRegistry.hpp 

# Colors
GREEN		= \033[0;32m
RED			= \033[0;31m
YELLOW		= \033[0;33m
NC			= \033[0m

# Rules
all: $(NAME)

$(NAME): $(OBJS)
	@echo "$(YELLOW)Linking $(NAME)...$(NC)"
	@$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)
	@echo "$(GREEN)✓ $(NAME) created successfully!$(NC)"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp $(HEADERS)
	@mkdir -p $(OBJ_DIR)
	@echo "$(YELLOW)Compiling $<...$(NC)"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@echo "$(RED)Removing object files...$(NC)"
	@$(RM) -r $(OBJ_DIR)
	@echo "$(GREEN)✓ Object files removed$(NC)"

fclean: clean
	@echo "$(RED)Removing $(NAME)...$(NC)"
	@$(RM) $(NAME)
	@$(RM) webserv.log
	@echo "$(GREEN)✓ $(NAME) removed$(NC)"

re: fclean all

run: $(NAME)
	@./$(NAME) config/default.conf

debug: CXXFLAGS += -g -fsanitize=address
debug: re

valgrind: $(NAME)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(NAME) config/default.conf

.PHONY: all clean fclean re run debug valgrind
