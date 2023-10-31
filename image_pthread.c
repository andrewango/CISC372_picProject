#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include "image.h"
#include <pthread.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

//An array of kernel matrices to be used for image convolution.  
//The indexes of these match the enumeration from the header file. ie. algorithms[BLUR] returns the kernel corresponding to a box blur.
Matrix algorithms[]={
    {{0,-1,0},{-1,4,-1},{0,-1,0}},
    {{0,-1,0},{-1,5,-1},{0,-1,0}},
    {{1/9.0,1/9.0,1/9.0},{1/9.0,1/9.0,1/9.0},{1/9.0,1/9.0,1/9.0}},
    {{1.0/16,1.0/8,1.0/16},{1.0/8,1.0/4,1.0/8},{1.0/16,1.0/8,1.0/16}},
    {{-2,-1,0},{-1,1,1},{0,1,2}},
    {{0,0,0},{0,1,0},{0,0,0}}
};


//getPixelValue - Computes the value of a specific pixel on a specific channel using the selected convolution kernel
//Paramters: srcImage:  An Image struct populated with the image being convoluted
//           x: The x coordinate of the pixel
//          y: The y coordinate of the pixel
//          bit: The color channel being manipulated
//          algorithm: The 3x3 kernel matrix to use for the convolution
//Returns: The new value for this x,y pixel and bit channel
uint8_t getPixelValue(Image* srcImage,int x,int y,int bit,Matrix algorithm){
    int px,mx,py,my,i,span;
    span=srcImage->width*srcImage->bpp;
    // for the edge pixes, just reuse the edge pixel
    px=x+1; py=y+1; mx=x-1; my=y-1;
    if (mx<0) mx=0;
    if (my<0) my=0;
    if (px>=srcImage->width) px=srcImage->width-1;
    if (py>=srcImage->height) py=srcImage->height-1;
    uint8_t result=
        algorithm[0][0]*srcImage->data[Index(mx,my,srcImage->width,bit,srcImage->bpp)]+
        algorithm[0][1]*srcImage->data[Index(x,my,srcImage->width,bit,srcImage->bpp)]+
        algorithm[0][2]*srcImage->data[Index(px,my,srcImage->width,bit,srcImage->bpp)]+
        algorithm[1][0]*srcImage->data[Index(mx,y,srcImage->width,bit,srcImage->bpp)]+
        algorithm[1][1]*srcImage->data[Index(x,y,srcImage->width,bit,srcImage->bpp)]+
        algorithm[1][2]*srcImage->data[Index(px,y,srcImage->width,bit,srcImage->bpp)]+
        algorithm[2][0]*srcImage->data[Index(mx,py,srcImage->width,bit,srcImage->bpp)]+
        algorithm[2][1]*srcImage->data[Index(x,py,srcImage->width,bit,srcImage->bpp)]+
        algorithm[2][2]*srcImage->data[Index(px,py,srcImage->width,bit,srcImage->bpp)];
    return result;
}


typedef struct convoluteParams {
        Image* srcImg;
        Image* destImg;
        Matrix algorithm;
        int thread_num;
        int thread_count;
} convoluteParams;

//convolute:  Applies a kernel matrix to an image
//Parameters: srcImage: The image being convoluted
//            destImage: A pointer to a  pre-allocated (including space for the pixel array) structure to receive the convoluted image.  It should be the same size as srcImage
//            algorithm: The kernel matrix to use for the convolution
//Returns: Nothing
void* convolute(void* imageParams){
	convoluteParams* imgParams = (convoluteParams*) imageParams;
    int row,pix,bit,span;
	Image* srcImage = imgParams->srcImg;
	Image* destImage = imgParams->destImg;
	Matrix algo;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			algo[i][j] = imgParams->algorithm[i][j];
		}
	}	
	int thread = imgParams->thread_num;
	int thread_count = imgParams->thread_count;
	int rows_per_thread = srcImage->height/thread_count;
    int first_index = thread * rows_per_thread;
    rows_per_thread = thread == thread_count - 1 ? srcImage->height - (rows_per_thread*(thread_count-1)) : rows_per_thread;
	int last_index = first_index + rows_per_thread;
	//printf("Thread number: %d | First index: %d | Last index: %d\n", thread, first_index, last_index-1);
    span=srcImage->bpp*srcImage->bpp;
    for (row=first_index;row<last_index;row++){
        for (pix=0;pix<srcImage->width;pix++){
            for (bit=0;bit<srcImage->bpp;bit++){
                destImage->data[Index(pix,row,srcImage->width,bit,srcImage->bpp)]=getPixelValue(srcImage,pix,row,bit,algo);
            }
        }
    }
	return NULL;
}

//Usage: Prints usage information for the program
//Returns: -1
int Usage(){
    printf("Usage: image <filename> <type> <number_of_threads>\n\twhere type is one of (edge,sharpen,blur,gauss,emboss,identity)\n");
    return -1;
}

//GetKernelType: Converts the string name of a convolution into a value from the KernelTypes enumeration
//Parameters: type: A string representation of the type
//Returns: an appropriate entry from the KernelTypes enumeration, defaults to IDENTITY, which does nothing but copy the image.
enum KernelTypes GetKernelType(char* type){
    if (!strcmp(type,"edge")) return EDGE;
    else if (!strcmp(type,"sharpen")) return SHARPEN;
    else if (!strcmp(type,"blur")) return BLUR;
    else if (!strcmp(type,"gauss")) return GAUSE_BLUR;
    else if (!strcmp(type,"emboss")) return EMBOSS;
    else return IDENTITY;
}

//main:
//argv is expected to take 2 arguments.  First is the source file name (can be jpg, png, bmp, tga).  Second is the lower case name of the algorithm.
int main(int argc,char** argv){
    long t1,t2;
    t1=time(NULL);

    stbi_set_flip_vertically_on_load(0); 
    if (argc!=4) return Usage();
    int thread_count = atoi(argv[3]);
	pthread_t* thread_handles = (pthread_t*)malloc(thread_count*sizeof(pthread_t));
	int thread;
    char* fileName=argv[1];
    if (!strcmp(argv[1],"pic4.jpg")&&!strcmp(argv[2],"gauss")){
        printf("You have applied a gaussian filter to Gauss which has caused a tear in the time-space continum.\n");
    }
    enum KernelTypes type=GetKernelType(argv[2]);

    Image srcImage,destImage,bwImage;   
    srcImage.data=stbi_load(fileName,&srcImage.width,&srcImage.height,&srcImage.bpp,0);
    if (!srcImage.data){
        printf("Error loading file %s.\n",fileName);
        return -1;
    }
    destImage.bpp=srcImage.bpp;
    destImage.height=srcImage.height;
    destImage.width=srcImage.width;
    destImage.data=malloc(sizeof(uint8_t)*destImage.width*destImage.bpp*destImage.height); 
	convoluteParams** paramArr = (convoluteParams**)malloc(sizeof(convoluteParams*)*thread_count);
	for (thread = 0; thread < thread_count; thread++) {
		convoluteParams* convoluteArgs = (convoluteParams*)malloc(sizeof(convoluteParams));
		convoluteArgs->srcImg = &srcImage;
		convoluteArgs->destImg = &destImage;
		convoluteArgs->thread_count = thread_count;
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				convoluteArgs->algorithm[i][j] = algorithms[type][i][j];
			}
		}	
		convoluteArgs->thread_num = thread;
		paramArr[thread] = convoluteArgs;
	}
	for (thread = 0; thread < thread_count; thread++) {
		convoluteParams* args = paramArr[thread];
		//printf("Thread %d running...\n", imgParams->thread);
		pthread_create(&thread_handles[thread], NULL, &convolute, (void*) args);
//		convolute(&srcImage,&destImage,algorithms[type]);
	}
    for (thread = 0; thread < thread_count; thread++) {
		//printf("Joined Thread %d\n", thread);
		pthread_join(thread_handles[thread], NULL);
        free(paramArr[thread]);
	}
    stbi_write_png("output.png",destImage.width,destImage.height,destImage.bpp,destImage.data,destImage.bpp*destImage.width);
	stbi_image_free(srcImage.data);
	free(thread_handles);
	free(paramArr);
    free(destImage.data);
    t2=time(NULL);
    printf("Took %ld seconds\n",t2-t1);
   return 0;
}
