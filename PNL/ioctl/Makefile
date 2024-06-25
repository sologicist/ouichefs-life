all : user defrag

user : user.c ioctl.h
	gcc  -o user user.c

defrag : defrag.c ioctl.h
	gcc  -o defrag defrag.c

clean :
	rm user defrag