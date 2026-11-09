volatile unsigned int value;
int main(){
	while(1){
	value = *(unsigned int*)0x43c00000;
	*(unsigned int*)0x43c00004 = value;
	}
}
