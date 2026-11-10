CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -Iinclude
LDFLAGS = -pthread

SRCDIR = src
OBJDIR = obj
BINDIR = .

SERVER_OBJS = $(OBJDIR)/server.o \
              $(OBJDIR)/network.o \
              $(OBJDIR)/session.o \
              $(OBJDIR)/db.o \
              $(OBJDIR)/customer.o \
              $(OBJDIR)/employee.o \
              $(OBJDIR)/manager.o \
              $(OBJDIR)/admin.o

CLIENT_OBJS = $(OBJDIR)/client.o \
              $(OBJDIR)/network.o

all: $(BINDIR)/server $(BINDIR)/client

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR)/server: $(SERVER_OBJS)
	$(CC) $(SERVER_OBJS) $(LDFLAGS) -o $@

$(BINDIR)/client: $(CLIENT_OBJS)
	$(CC) $(CLIENT_OBJS) $(LDFLAGS) -o $@

clean:
	rm -rf $(OBJDIR) $(BINDIR)/server $(BINDIR)/client

reset-db:
	bash scripts/init_db.sh

.PHONY: all clean reset-db
