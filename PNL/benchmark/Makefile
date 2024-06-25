all : benchmark_fd benchmark_file

benchmark_fd : benchmark_fd.c
	gcc -o benchmark_fd benchmark_fd.c

benchmark_file : benchmark_file.c
	gcc -o benchmark_file benchmark_file.c

test : benchmark_test.sh
	./benchmark_test

clean :
	rm benchmark_fd benchmark_file
	rm test/*
