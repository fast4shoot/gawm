gawm: Makefile main.cpp utils.hpp window.hpp
	# Jsem proste Biba
	g++ -o gawm -std=c++11 main.cpp -lGL -lX11 -lXcomposite -g3

run: gawm
	killall gawm 2>/dev/null; xinit ./gawm -- /usr/bin/Xephyr :2 &

