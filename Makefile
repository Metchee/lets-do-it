NAME = plazza

CXX = g++
CXXFLAGS = -Wall -Wextra -Werror -std=c++14 -Iinclude -pthread

SRCDIR = src
INCDIR = include
OBJDIR = obj

SOURCES = $(shell find $(SRCDIR) -name "*.cpp")
OBJECTS = $(SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

.PHONY: all clean fclean re

all: $(NAME)

$(NAME): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(NAME) $(CXXFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all