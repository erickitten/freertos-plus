#include "customfunc.h"
#include <stdlib.h>

int prime_check(int arg){
	int i;
    for(i=2;i<=(arg>>1);i++){
        if(arg%i==0){
            return 0;
            break;
        }
    }
    return 1;
}

int fibonacci(int arg){
    if(arg <=0){return 0;}
    else if(arg-- == 1){return 1;}
    else{
        int a=1,b=1;
        while(1){
            if(arg-- == 1){
                return b;
            };
            a += b;
            if(arg-- == 1){
                return a;
            };
            b += a;
		}
	}
}
