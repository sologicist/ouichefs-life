all : benchmark_fd benchmark_file user defrag create_test

benchmark_fd : benchmark_fd.c
	gcc -o benchmark_fd benchmark_fd.c

benchmark_file : benchmark_file.c
	gcc -o benchmark_file benchmark_file.c

test : benchmark_test.sh
	./benchmark_test

user : user.c ioctl.h
	gcc  -o user user.c

defrag : defrag.c ioctl.h
	gcc  -o defrag defrag.c

create_test : 
	cd ./ouichefs && mkdir -p test && cd ..

clean :
	rm benchmark_fd benchmark_file
	rm user defrag
	rm -f test/*
