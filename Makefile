##
# Makefile - make support
#
# Copyright (C) 2010 Felipe Balbi <balbi@ti.com
#
# This is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public Liicense as published by
# the Free Software Foundation, either version 3 of the license, or
# (at your option) any later version.
#
# It is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this file. If not, see <http://www.gnu.org/licenses/>.
##

PREFIX=$(HOME)/.local

#
# Pretty print
#
V		= @
Q		= $(V:1=)
QUIET_CC	= $(Q:@=@echo    '     CC       '$@;)
QUIET_CLEAN	= $(Q:@=@echo    '     CLEAN    '$@;)
QUIET_LINK	= $(Q:@=@echo    '     LINK     '$@;)

PROGRAM		= bk1785b
SOURCES		= bk1785b.c
OBJECTS		= $(SOURCES:.c=.o)

all: $(SOURCES) $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(QUIET_LINK) $(CC) $(OBJECTS) -o $@

.c.o:
	$(QUIET_CC) $(CC) -c $< -o $@

clean:
	$(QUIET_CLEAN) rm -f $(OBJECTS) $(PROGRAM) *~ tags

install:
	install -m 744 $(PROGRAM) $(PREFIX)/bin/$(PROGRAM)

