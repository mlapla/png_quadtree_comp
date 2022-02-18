#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <png.h>
#define PNG_DEBUG 3

/*
  Michael Laplante, July 2020.
  Image Quadtree Compression
  ---------------------------
  This program interprets images and subdivides it into squares of varying
  sizes to estimate regions of similar colors. The result is an encoding
  that can reduce a lossless image into a compressed image containing
  less data.
*/

typedef struct quad_node quad_node;
struct quad_node {
  void* value;
  quad_node* parent;
  quad_node* q1;
  quad_node* q2;
  quad_node* q3;
  quad_node* q4;
};

typedef struct quad_tree quad_tree;
struct quad_tree {
  quad_node* root;
};

// png.h structure
typedef struct image_data image_data;
struct image_data {
  png_structp png_ptr;
  png_infop info_ptr;
  png_byte color_type;
  png_byte bit_depth;
  int interlace_amount;
  png_bytep* row_pointers;
  int width;
  int height;
  char file_name[50];
};

typedef struct png_pixel png_pixel;
struct png_pixel {
  png_byte red;
  png_byte blue;
  png_byte green;
  png_byte alpha;
};

void CopyImageSettings(image_data*, image_data);
void ImageRowAlloc(image_data*, int, int);
png_pixel** ImageToMatrix(image_data*);
void MatrixToImage(image_data*, png_pixel**);
quad_tree* MatrixToQuad(png_pixel**, int, int);
png_pixel** QuadToMatrix(quad_tree*, int, int);
quad_node* RecMatToNode(quad_node*, png_pixel**, int, int);
void RecNodeToMat(png_pixel**, quad_node*, int, int, int, int);
png_pixel*** SplitImgIn4(png_pixel** mat, nt width, int height);
void Compress(quad_tree*, float);
void PruneBranches(quad_node*, float);
png_pixel PixelAverage(png_pixel*, int);
float PixelColorDistance(png_pixel, png_pixel);
float PixelVariance(png_pixel, png_pixel*, int );
void ReadImage(image_data*);
void WriteImage(image_data);
quad_tree* qt_alloc(quad_node*);
void qt_free(quad_tree*);
quad_node* qn_alloc(void*,quad_node*);
void qn_free(quad_node*);
png_pixel** image_alloc(int, int);

////////////////
/*    main    */
////////////////
int main(int argc, char* const argv[]){

  // CLI
  if(argc != 2){
    printf("Usage: program_name <file_in>");
    return 1;
  }

  // Compression parameter
  // Threshold above which the information can't be compressed
  // Normalized from 0.0 to 1.0. Common values around 0.001
  float compressionThreshold = 0.0005;
  
  // Get Image:
  image_data input_img;
  memcpy(input_img.file_name,argv[1],strlen(argv[1])+1);
  strcpy(input_img.file_name,argv[1]);
  ReadImage(&input_img);
  
  // Compression:
  png_pixel** in_data = ImageToMatrix(&input_img);
  quad_tree* qt = MatrixToQuad(in_data,input_img.width,input_img.height);
  Compress(qt,compressionThreshold);

  // Write output:
  png_pixel** out_data = QuadToMatrix(qt,input_img.width,input_img.height);
  image_data output_img;
  CopyImageSettings(&output_img,input_img);
  ImageRowAlloc(&output_img,png_get_rowbytes(input_img.png_ptr,input_img.info_ptr),input_img.height);
  MatrixToImage(&output_img,out_data);
  WriteImage(output_img);
  
  return 0;
}

/**
 * Copies the settings of a png image to another one
 * @param out the image pointer to paste settings to
 * @param in  the image to get settings from
 */
void CopyImageSettings(image_data* out, image_data in){

  out -> color_type = in.color_type;
  out -> bit_depth = in.bit_depth;
  out -> interlace_amount = in.interlace_amount;
  out -> width = in.width;
  out -> height = in.height;

}

/**
 * Allocates memory for the rows of pixels of the image
 * @param out       pointer to the allocated image
 * @param row_bytes number of bytes to allocate per row
 * @param height    number of rows in the image
 */
void ImageRowAlloc(image_data* out, int row_bytes, int height){
  out -> row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
  for(int y = 0; y < height; y++) {
    out -> row_pointers[y] = (png_byte*)malloc(row_bytes);
  }
}

/**
 * Creates a matrix holding the pixels of the image, a pixel map
 * @param img the image to get a pixel map of
 * @return the pixel map matrix
 */
png_pixel** ImageToMatrix(image_data* img){
  
  png_pixel** mat = image_alloc(img -> width,img -> height);

  for(int y = 0; y < img -> height; y++){
    png_bytep row = (img -> row_pointers)[y];
    for(int x = 0; x < img -> width; x++){

      png_bytep pix = &(row[x * 4]);
      png_pixel new_pix;
      new_pix.red = pix[0];
      new_pix.green = pix[1];
      new_pix.blue = pix[2];
      new_pix.alpha = pix[3];
      mat[y][x] = new_pix;

    }
  }

  return mat;
}

/**
 * Writes a matrix of pixels to an image
 * @param img_out the image to write to
 * @param mat     the matrix to get pixels from
 */
void MatrixToImage(image_data* img_out, png_pixel** mat){

  for(int y = 0; y < img_out -> height; y++){
    png_bytep row = (img_out -> row_pointers)[y];
    for(int x = 0; x < img_out -> width; x++){

      png_bytep pix = &(row[x * 4]);
      png_pixel new_pix = mat[y][x];
      pix[0] = new_pix.red;
      pix[1] = new_pix.green;
      pix[2] = new_pix.blue;
      pix[3] = new_pix.alpha;

    }
  }
}

/**
 * Constructs a quadtree from a matrix 
 * @param mat    matrix
 * @param width  width of the matrix
 * @param height height of the matrix
 * @return pointer to the quadtree
 */
quad_tree* MatrixToQuad(png_pixel** mat,int width,int height){
  quad_tree* qt = qt_alloc(NULL);
  quad_node* root_ = qn_alloc(NULL,NULL);
  qt -> root = RecMatToNode(root_,mat,width,height);
  
  return qt;
}

/**
 * Constructs the matrix corresponding to a quadtree
 * @param qt      quadtree to convert
 * @param width   width of the target matrix
 * @param height  height of the target matrix
 * @return the matrix
 */
png_pixel** QuadToMatrix(quad_tree* qt,int width,int height){

  png_pixel** mat = image_alloc(width,height);

  RecNodeToMat(mat,qt -> root,0,0,width,height);
  
  return mat;
}

/**
 * Helper recursion fonction to convert a portion 
 * of a matrix to a quadtree node
 * @param parent  parent node of the quadtree node
 * @param mat     matrix to get pixels from
 * @param width   width of the remaining sub-matrix to convert
 * @param height  height of the remaining sub-matrix to convert
 * @return quadtree node
 */
quad_node* RecMatToNode(quad_node* parent, png_pixel** mat, 
                        int width, int height){

  if(width == 0 || height == 0)
    printf("\nWidth or height was not a power of 2.\n");
  
  if(width == 1 && height == 1)
    return qn_alloc(&mat[0][0],parent);

  // Subdivide the image into 2x2 pieces:
  png_pixel*** sub_imgs = SplitImgIn4(mat,width,height);

  quad_node* child = qn_alloc(NULL,parent);
  child -> q1 = RecMatToNode(child,sub_imgs[0],width/2,height/2);
  child -> q2 = RecMatToNode(child,sub_imgs[1],width/2,height/2);
  child -> q3 = RecMatToNode(child,sub_imgs[2],width/2,height/2);
  child -> q4 = RecMatToNode(child,sub_imgs[3],width/2,height/2);

  return child;
}

/**
 * Helper recursion fonction to convert a portion 
 * of a matrix to a quadtree node
 * @param mat     matrix to write pixels to
 * @param node    current node to read in the recursion
 * @param x       position (horizontal) of the node
 * @param y       position (vertical) of the node
 * @param width   width of the matrix
 * @param height  height of the matrix
 */
void RecNodeToMat(png_pixel** mat, quad_node* node, 
                  int x,int y,int width,int height){

  // If node is at the pixel level, draw and end recursion.
  if(width == 1 && height == 1){
    mat[y][x] = *(png_pixel*)(node -> value);
    return;
  }

  // If leaf ends, keep the same color.
  if(node -> q1 == NULL && node -> q2 == NULL &&
     node -> q3 == NULL && node -> q4 == NULL){
    node -> q1 = node;
    node -> q2 = node;
    node -> q3 = node;
    node -> q4 = node;
  }

  // Top-left
  RecNodeToMat(mat, node -> q1,
              x, y, width/2, height/2);
  // Top-right
  RecNodeToMat(mat, node -> q2,
              x + (width/2), y, width/2, height/2);
  // Bottom-left
  RecNodeToMat(mat, node -> q3,
              x, y + (height/2), width/2, height/2);
  // Bottom-right
  RecNodeToMat(mat, node -> q4,
              x + (width/2), y + (height/2), width/2, height/2);
}

/**
 * Splits and allocates a pixel matrix in 4 sub-matrices 
 * along the center.
 * @param mat    matrix of pixels
 * @param width  width of the matrix
 * @param height height of the matrix
 * @return pointer to the 4 sub-matrices
 */
png_pixel*** SplitImgIn4(png_pixel** mat, int width, int height){

  png_pixel*** sub_imgs = (png_pixel***) malloc(4*sizeof(png_pixel**));

  sub_imgs[0] = image_alloc(width/2,height/2);
  sub_imgs[1] = image_alloc(width/2,height/2);
  sub_imgs[2] = image_alloc(width/2,height/2);
  sub_imgs[3] = image_alloc(width/2,height/2);

  for(int x = 0; x < width/2; x++){
    for(int y = 0; y < height/2; y++){
      //Top left
      sub_imgs[0][y][x] = mat[y][x];
      //Top right
      sub_imgs[1][y][x] = mat[y][x+(width/2)];
      //Bottom left
      sub_imgs[2][y][x] = mat[y+(height/2)][x];
      //Bottom right
      sub_imgs[3][y][x] = mat[y+(height/2)][x+(width/2)];
    }
  }

  return sub_imgs;
}

/**
 * Applies compression to a quadtree to branches with similar colors.
 * @param qt                    the quadtree to compress
 * @param compressionThreshold  the threshold to compress according to.
 */
void Compress(quad_tree* qt, float compressionThreshold){
  PruneBranches(qt -> root,compressionThreshold);
}

/**
 * Recursively removes branches of a quadtree that have a similar color,
 * according to a compression threshold.
 * @param node                 the sub-branch to prune
 * @param compressionThreshold the threshold to user to compare colors with.
 */
void PruneBranches(quad_node* node, float compressionThreshold){

  if(node -> q1 != NULL && node -> q2 != NULL &&
     node -> q3 != NULL && node -> q4 != NULL){

    // Compress leaves, then move upwards
    PruneBranches(node -> q1,compressionThreshold);
    PruneBranches(node -> q2,compressionThreshold);
    PruneBranches(node -> q3,compressionThreshold);
    PruneBranches(node -> q4,compressionThreshold);

    // If a leaf was not compressed, don't compress.
    if( node -> q1 -> value == NULL || node -> q2 -> value == NULL ||
        node -> q3 -> value == NULL || node -> q4 -> value == NULL)
      return;

    // Get pixels
    png_pixel* list_pixels = malloc(sizeof(png_pixel) * 4);
    list_pixels[0] = *((png_pixel*) node -> q1 -> value);
    list_pixels[1] = *((png_pixel*) node -> q2 -> value);
    list_pixels[2] = *((png_pixel*) node -> q3 -> value);
    list_pixels[3] = *((png_pixel*) node -> q4 -> value);

    png_pixel avg = PixelAverage(list_pixels,4);

    //If 4 squares are of similar color, combine them.
    if(PixelVariance(avg,list_pixels,4) < compressionThreshold){
      //Cut the tree
      node -> value = (png_pixel*)malloc(sizeof(png_pixel));
      ((png_pixel*)node -> value) -> red = avg.red;
      ((png_pixel*)node -> value) -> green = avg.green;
      ((png_pixel*)node -> value) -> blue = avg.blue;
      ((png_pixel*)node -> value) -> alpha = avg.alpha;
      node -> q1 = NULL;
      node -> q2 = NULL;
      node -> q3 = NULL;
      node -> q4 = NULL;
    }

    return;
  }
}

/**
 * Computes the average color of pixels.
 * @param list_pixels list of pixels to average
 * @param count       the number of pixels in the list
 * @return a pixel with the average color
 */
png_pixel PixelAverage(png_pixel* list_pixels,int count){

  png_pixel avg;

  float avg_red = 0.0;
  float avg_green = 0.0;
  float avg_blue = 0.0;
  float avg_alpha = 0.0;

  for (int n = 0; n < count; n++){
    avg_red += list_pixels[n].red;
    avg_blue += list_pixels[n].blue;
    avg_green += list_pixels[n].green;
    avg_alpha += list_pixels[n].alpha;
  }

  avg.red = (png_byte) (avg_red / ((float) count));
  avg.blue = (png_byte) (avg_blue / ((float) count));
  avg.green = (png_byte) (avg_green / ((float) count));
  avg.alpha = (png_byte) (avg_alpha / ((float) count));

  return avg;
}

/**
 * Computes the "distance" between two pixels in color space.
 * @param pix1 first pixel
 * @param pix2 second pixel
 * @return distance between pix1 and pix2
 */
float PixelColorDistance(png_pixel pix1, png_pixel pix2){

  float distance = 0;
  //Vector distance
  distance += pow(pix1.red - pix2.red,2);
  distance += pow(pix1.green - pix2.green,2);
  distance += pow(pix1.blue - pix2.blue,2);
  distance += pow(pix1.alpha - pix2.alpha,2);

  float max_distance = pow(2 * 256,2) * 4;

  return distance/max_distance; // Normalized
}

/**
 * Computes the variance of the distance between the pixels.
 * TODO: internally compute the average rather then take it as an argument.
 * @param average_pix the average pixel of the list of pixels
 * @param list_pixels a list of pixels to compute the variance of
 * @param count number of pixels
 * @return the variance of the distance between the pixels
 */
float PixelVariance(png_pixel average_pix, png_pixel* list_pixels,int count){

  float variance = 0;
  
  for(int n = 0; n < count; n++){
    //Vector distance
    variance += PixelColorDistance(average_pix,list_pixels[n]);
  }

  return variance/((float)count);
}

/**
 * Read an image from a <png.h> image_data struct with the filename loaded.
 * TODO: read only a file name and return the image instead.
 * @param img pointer to load the data to.
 */
void ReadImage(image_data* img){
  
  unsigned char header[8];

  printf("Reading: %s\n",img -> file_name);

  //Open file...
  FILE *fp = fopen(img -> file_name,"rb");
  if(!fp)
    printf("Oops, file not available or not found.");

  //Read header...
  fread(header,1,8,fp);

  if(png_sig_cmp(header,0,8))
    printf("File is not PNG.");

  img -> png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
  img -> info_ptr = png_create_info_struct(img -> png_ptr);
  
  if(setjmp(png_jmpbuf(img -> png_ptr)))
    printf("Set jmp failed.");

  png_init_io(img -> png_ptr,fp);
  png_set_sig_bytes(img -> png_ptr,8);

  png_read_info(img -> png_ptr,img -> info_ptr);

  img -> width = png_get_image_width(img -> png_ptr,img -> info_ptr);
  img -> height = png_get_image_height(img -> png_ptr,img -> info_ptr);
  img -> color_type = png_get_color_type(img -> png_ptr,img -> info_ptr);
  img -> bit_depth = png_get_bit_depth(img -> png_ptr,img -> info_ptr);
  img -> interlace_amount = png_set_interlace_handling(img -> png_ptr);

  png_read_update_info(img -> png_ptr,img -> info_ptr);  

  //Allocate memory to receive image...
  if(setjmp(png_jmpbuf(img -> png_ptr)))
    printf("Set second jmp failed.");

  img -> row_pointers = (png_bytep*) malloc(sizeof(png_bytep) * img -> height);
  for(int y=0; y< img -> height; y++){
    img -> row_pointers[y] = (png_byte*) malloc(png_get_rowbytes(img -> png_ptr,img -> info_ptr));
  }

  png_read_image(img -> png_ptr,img -> row_pointers);
  fclose(fp);

  //Output
  printf("--Image loaded--\n");
  printf("| Width x Height: %d %d\n",img -> width,img -> height);
  printf("| Color Type: %d (RGB is %d, RGBA is %d)\n",
	 img -> color_type,PNG_COLOR_TYPE_RGB,PNG_COLOR_TYPE_RGBA);
  printf("| Bit depth: %d\n",img -> bit_depth);
  printf("| Interlace level: %d\n",img -> interlace_amount);
}

/**
 * Write an image from a <png.h> image_data struct.
 * TODO: get the output file name from args.
 * @param img pointer to load the data from.
 */
void WriteImage(image_data img){

  printf("Writing image...");
  FILE *fp = fopen("output.png","wb");
  if(!fp)
    printf("Write file couldn't be opened.");

  //Init
  img.png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
  img.info_ptr = png_create_info_struct(img.png_ptr);
  png_init_io(img.png_ptr,fp);

  //Write header
  if(setjmp(png_jmpbuf(img.png_ptr)))
    printf("Write header setup failed");

  png_set_IHDR(img.png_ptr,img.info_ptr,img.width,img.height,img.bit_depth,img.color_type,
	       PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_BASE,PNG_FILTER_TYPE_BASE);

  png_write_info(img.png_ptr,img.info_ptr);

  if(setjmp(png_jmpbuf(img.png_ptr)))
    printf("Write bytes failed.");

  png_write_image(img.png_ptr,img.row_pointers);

  if(setjmp(png_jmpbuf(img.png_ptr)))
    printf("Error in end of write.");

  png_write_end(img.png_ptr,NULL);

  //Cleanup
  for(int y = 0; y < img.height;y++){
    free(img.row_pointers[y]);
  }
  free(img.row_pointers);

  //Close
  fclose(fp);
  printf("File closed.\n");
}

/**
 * Allocates a quadtree node and assigns its values.
 * @param val value to assign to the node
 * @param par parent node of the node
 * @return pointer to the quadtree node
 */
quad_node* qn_alloc(void* val, quad_node* par){
  quad_node* qn = (quad_node*) malloc(sizeof(quad_node));
  qn -> value = val;
  qn -> parent = par;
  return qn;
}

/**
 * Frees the allocated memory of a quadtree node.
 * @param qn the node to free
 */
void qn_free(quad_node* qn){

  if(qn == NULL)
    return;
  else {
    qn_free(qn -> q1);
    qn_free(qn -> q2);
    qn_free(qn -> q3);
    qn_free(qn -> q4);
    if(qn -> value != NULL)
      free(qn -> value);
    free(qn);
  }
}

/**
 * Allocates memory for a quadtree pointer.
 * @param qn the root node to assign as the first node of the quadtree
 * @return the allocated quadtree pointer
 */
quad_tree* qt_alloc(quad_node* qn){
  quad_tree* qt = (quad_tree*)malloc(sizeof(quad_tree)); 
  qt -> root = qn;
  return qt;
}

/**
 * Frees the allocated memory of a quadtree
 * @param qt the quadtree to free
 */
void qt_free(quad_tree* qt){
  qn_free(qt -> root);
  free(qt);
  return;
}

/**
 * Allocates the memory for a matrix of pixels (pixel map of an image).
 * @param width  number of rows to allocate
 * @param height number of columns to allocate
 * @return the allocated matrix of pixels
 */
png_pixel** image_alloc(int width, int height){

  png_pixel** mat = (png_pixel**) malloc(height * sizeof(png_pixel*));

  for(int i = 0; i < width; i++){
    mat[i] = malloc(width * sizeof(png_pixel));
  }

  return mat;
}