# -*- coding: utf-8 -*-
#
# Makefile pro projekt GAWM
#

# Jméno přeloženého programu
program=gawm

# Číslo displeje pro Xephyr
display=:2

# Úroveň množství debugovacích informací
LDB=-g3 -DDEBUG

# Seznam ostatních souborů.
OTHER=Makefile

# Překladač C
CPP=g++
CXX=$(CPP)

# Link
LINK=-lGLEW -lGL -lX11 -lXcomposite -lXdamage -lXfixes

# Makra
# MACROS=-D_XOPEN_SOURCE -D_XOPEN_SOURCE_EXTENDED

# Nepovinné parametry překladače
BRUTAL=-Wall -Wextra -Werror -Wno-unused-variable
CXXFLAGS=-std=c++11 $(STRICT) -pedantic $(MACROS)

SOURCES=main gawmGl window winmgr

$(program): $(addprefix obj/,$(addsuffix .o,$(SOURCES)))
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LINK)

obj/%.o: src/%.cpp
	mkdir -p dep obj # Adresare nejsou v gitu
	$(CXX) -MMD -MP -MF dep/$*.d -c -o $@ $< $(CXXFLAGS) -DNDEBUG

obj/dbg/%.o: src/%.cpp
	mkdir -p dep/dbg obj/dbg # Adresare nejsou v gitu
	$(CXX) -MMD -MP -MF dep/dbg/$*.d -c -o $@ $< $(CXXFLAGS) $(BRUTAL) $(LDB)

-include $(addprefix dep/,$(addsuffix .d,$(SOURCES)))

#######################################################################
.PHONY: build strict clean pack test_ test testSmall run valgrind kdbg debug

# Zkompiluje program (výchozí)
build: $(program)

strict:
	make "STRICT=$(BRUTAL)"

# Spustí testy
run: $(program)
	xinit './$(program)' -- /usr/bin/Xephyr $(display) -host-cursor -screen 800x600 &

# Pro Bibu, kterej ma malej kompl
runSmall: $(program)
	xinit './$(program)' -- /usr/bin/Xephyr $(display) -host-cursor -screen 640x480 &

runFull: $(program)
	xinit './$(program)' -- /usr/bin/Xephyr $(display) -host-cursor -fullscreen &

runDemo: $(program)
	xinit './$(program)' -- /usr/bin/Xephyr $(display) -host-cursor -screen 1280x720 &

# Nespouštět, pokud neběží gawm!
test_:
	runner=$$(bash ./get_pid.sh './$(program)')
	DISPLAY=$(display) xterm -geometry 68x29+30+30 &
	DISPLAY=$(display) xterm -geometry 100x17+100+60 &

demo_:
	runner=$$(bash ./get_pid.sh './$(program)')
	DISPLAY=$(display) sakura &
	DISPLAY=$(display) sakura &
	DISPLAY=$(display) gnome-calculator &
	DISPLAY=$(display) pidgin &
	DISPLAY=$(display) glxgears &
	DISPLAY=$(display) geany &
	DISPLAY=$(display) nemo --no-desktop &
	DISPLAY=$(display) gimp &
	DISPLAY=$(display) eog ~/Obrázky/blue-abstract-wide-wallpaper-1680x1050-010.jpg &
	DISPLAY=$(display) nemo --no-desktop &

test: run test_

# Pro Bibu, kterej ma malej kompl
testSmall: runSmall test_

testFull: runFull test_

testDemo: runDemo demo_

# Test s více výpisy
testDbg: debug
	xinit './$(program)-dbg' -- /usr/bin/Xephyr $(display) -screen 640x480 $(CURSOR) &
	runner=$$(bash ./get_pid.sh './$(program)-dbg')
	DISPLAY=$(display) xterm -geometry 68x29+30+30 &
	DISPLAY=$(display) xterm -geometry 100x17+100+60 &

# Test když se nezobrazuje kurzor
testCursor:
	make 'CURSOR=-host-cursor' testDbg

valgrind: debug
	/usr/bin/Xephyr $(display) & \
	xephyr_p=$$!; \
	DISPLAY=$(display) valgrind --tool=memcheck --leak-check=yes --show-leak-kinds=definite './$(program)-dbg'; \
	kill $$xephyr_p;

valgrind_reachable: debug
	/usr/bin/Xephyr $(display) & \
	xephyr_p=$$!; \
	DISPLAY=$(display) valgrind --tool=memcheck --leak-check=yes --show-leak-kinds=all './$(program)-dbg'; \
	kill $$xephyr_p;

gdb: debug
	/usr/bin/Xephyr $(display) & \
	xephyr_p=$$!; \
	DISPLAY=$(display) gdb './$(program)-dbg'; \
	kill $$xephyr_p;

kdbg: debug
	xinit './$(program)-dbg' -- /usr/bin/Xephyr $(display) & \
	xinit_p=$$!; \
	bash ./debug-kdbg.sh; \
	kill $$xinit_p;

#  Debug
#  *****

debug: $(program)-dbg

$(program)-dbg: $(addprefix obj/dbg/,$(addsuffix .o,$(SOURCES)))
	$(CXX) -o '$(program)-dbg' $^ $(CXXFLAGS) $(BRUTAL) $(LDB) $(LINK)

clean:
	rm -f obj/*.o obj/dbg/*.o dep/*.d dep/dbg/*.d './$(program)' './$(program)-dbg'
	rm -f pack/$(program) pack/*.*

pack:
	make -C doc
	rm -f pack/$(program) pack/*.*
	bash -c 'cp src/*[^~] pack/; cp doc/$(program).pdf pack/dokumentace.pdf; cd pack; tar -zcvf ../xbiber00.tgz *'
