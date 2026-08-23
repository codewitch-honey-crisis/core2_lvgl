/**
 * @file lv_fs_stdio.c
 *
 */


/*********************
 *      INCLUDES
 *********************/
#include <Arduino.h>
#include <SD.h>
#include <lvgl.h>


#include <stdio.h>
#ifndef WIN32
    #include <dirent.h>
    #include <unistd.h>
#else
    #include <windows.h>
#endif

/*********************
 *      DEFINES
 *********************/
#define MAX_PATH_LEN 256

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
#ifdef WIN32
    HANDLE dir_p;
    char next_fn[MAX_PATH_LEN];
#else
    DIR * dir_p;
#endif
} dir_handle_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void * sd_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode);
static lv_fs_res_t sd_close(lv_fs_drv_t * drv, void * file_p);
static lv_fs_res_t sd_read(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br);
static lv_fs_res_t sd_write(lv_fs_drv_t * drv, void * file_p, const void * buf, uint32_t btw, uint32_t * bw);
static lv_fs_res_t sd_seek(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence);
static lv_fs_res_t sd_tell(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p);
static void * sd_dir_open(lv_fs_drv_t * drv, const char * path);
static lv_fs_res_t sd_dir_read(lv_fs_drv_t * drv, void * dir_p, char * fn);
static lv_fs_res_t sd_dir_close(lv_fs_drv_t * drv, void * dir_p);

/**********************
 *  STATIC VARIABLES
 **********************/
static File sd_file[5];
static File sd_file_tmp;
/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
static int sd_find_free_index() {
    for(int i = 0;i<sizeof(sd_file)/sizeof(File);++i) {
        if(!sd_file[i]) {
            return i;
        }
    }
    return -1;
}
/**
 * Register a driver for the File system interface
 */
void lv_fs_sd_init(void)
{
    /*---------------------------------------------------
     * Register the file system interface in LVGL
     *--------------------------------------------------*/

    /*Add a simple drive to open images*/
    static lv_fs_drv_t fs_drv; /*A driver descriptor*/
    lv_fs_drv_init(&fs_drv);

    /*Set up fields...*/
    fs_drv.letter = 'S';
    fs_drv.cache_size = 0;

    fs_drv.open_cb = sd_open;
    fs_drv.close_cb = sd_close;
    fs_drv.read_cb = sd_read;
    fs_drv.write_cb = sd_write;
    fs_drv.seek_cb = sd_seek;
    fs_drv.tell_cb = sd_tell;

    fs_drv.dir_close_cb = sd_dir_close;
    fs_drv.dir_open_cb = sd_dir_open;
    fs_drv.dir_read_cb = sd_dir_read;

    lv_fs_drv_register(&fs_drv);
    Serial.println("lv_fs_sd_init()");
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Open a file
 * @param drv pointer to a driver where this function belongs
 * @param path path to the file beginning with the driver letter (e.g. S:/folder/file.txt)
 * @param mode read: FS_MODE_RD, write: FS_MODE_WR, both: FS_MODE_RD | FS_MODE_WR
 * @return pointer to FIL struct or NULL in case of fail
 */
static void * sd_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);
    Serial.println("sd_open()");
    const char * flags = "";

    if(mode == LV_FS_MODE_WR) flags = "wb";
    else if(mode == LV_FS_MODE_RD) flags = "rb";
    else if(mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) flags = "rb+";

    /*Make the path relative to the current directory (the projects root folder)*/
    int sdi =  sd_find_free_index();
    if(sdi==-1) return nullptr;
    sd_file[sdi] = SD.open(path,flags);
    if(!sd_file[sdi]) {
        return nullptr;
    }
    Serial.println("got file");
    return &sd_file[sdi];
}

/**
 * Close an opened file
 * @param drv pointer to a driver where this function belongs
 * @param file_p pointer to a FILE variable. (opened with fs_open)
 * @return LV_FS_RES_OK: no error, the file is read
 *         any error from lv_fs_res_t enum
 */
static lv_fs_res_t sd_close(lv_fs_drv_t * drv, void * file_p)
{
    LV_UNUSED(drv);
    File& sdf = *(File*)file_p;
    sdf.close();
    return LV_FS_RES_OK;
}

/**
 * Read data from an opened file
 * @param drv pointer to a driver where this function belongs
 * @param file_p pointer to a FILE variable.
 * @param buf pointer to a memory block where to store the read data
 * @param btr number of Bytes To Read
 * @param br the real number of read bytes (Byte Read)
 * @return LV_FS_RES_OK: no error, the file is read
 *         any error from lv_fs_res_t enum
 */
static lv_fs_res_t sd_read(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br)
{
    LV_UNUSED(drv);
    File& sdf = *(File*)file_p;
    uint32_t result = sdf.read((uint8_t*)buf,btr);
    *br = result;
    return (int32_t)(*br) < 0 ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}

/**
 * Write into a file
 * @param drv pointer to a driver where this function belongs
 * @param file_p pointer to a FILE variable
 * @param buf pointer to a buffer with the bytes to write
 * @param btw Bytes To Write
 * @param bw the number of real written bytes (Bytes Written). NULL if unused.
 * @return LV_FS_RES_OK or any error from lv_fs_res_t enum
 */
static lv_fs_res_t sd_write(lv_fs_drv_t * drv, void * file_p, const void * buf, uint32_t btw, uint32_t * bw)
{
    LV_UNUSED(drv);
    File& sdf = *(File*)file_p;
    uint32_t result = sdf.write((uint8_t*)buf,btw);
    *bw = result;
    return (int32_t)(*bw) < 0 ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}

/**
 * Set the read write pointer. Also expand the file size if necessary.
 * @param drv pointer to a driver where this function belongs
 * @param file_p pointer to a FILE variable. (opened with fs_open )
 * @param pos the new position of read write pointer
 * @return LV_FS_RES_OK: no error, the file is read
 *         any error from lv_fs_res_t enum
 */
static lv_fs_res_t sd_seek(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    File& sdf = *(File*)file_p;
    switch(whence) {
        case LV_FS_SEEK_CUR:
            sdf.seek(pos,SeekCur);
            break;
        case LV_FS_SEEK_END:
            sdf.seek(pos,SeekEnd);
            break;
        default: // LV_FS_SEEK_SET
            sdf.seek(pos,SeekSet);
            break;
    }
    return LV_FS_RES_OK;
}

/**
 * Give the position of the read write pointer
 * @param drv pointer to a driver where this function belongs
 * @param file_p pointer to a FILE variable.
 * @param pos_p pointer to to store the result
 * @return LV_FS_RES_OK: no error, the file is read
 *         any error from lv_fs_res_t enum
 */
static lv_fs_res_t sd_tell(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p)
{
    LV_UNUSED(drv);
    File& sdf = *(File*)file_p;
    uint32_t result = sdf.position();
    *pos_p = result;
    return LV_FS_RES_OK;
}

/**
 * Initialize a 'DIR' or 'HANDLE' variable for directory reading
 * @param drv pointer to a driver where this function belongs
 * @param path path to a directory
 * @return pointer to an initialized 'DIR' or 'HANDLE' variable
 */
static void * sd_dir_open(lv_fs_drv_t * drv, const char * path)
{
    LV_UNUSED(drv);
    int sdi =  sd_find_free_index();
    if(sdi==-1) return nullptr;
    sd_file[sdi]=SD.open(path);
    File& sdf = sd_file[sdi];
    if(!sdf || !sdf.isDirectory()) {
        return nullptr;
    }
    return &sdf;
}

/**
 * Read the next filename form a directory.
 * The name of the directories will begin with '/'
 * @param drv pointer to a driver where this function belongs
 * @param dir_p pointer to an initialized 'DIR' or 'HANDLE' variable
 * @param fn pointer to a buffer to store the filename
 * @return LV_FS_RES_OK or any error from lv_fs_res_t enum
 */
static lv_fs_res_t sd_dir_read(lv_fs_drv_t * drv, void * dir_p, char * fn)
{
    LV_UNUSED(drv);
    File& sdf = *(File*)dir_p;
    if(!sdf.isDirectory()) {
        return LV_FS_RES_INV_PARAM;
    }
    bool isDir=false;
    String sfn = sdf.getNextFileName(&isDir);
    if(sfn.length()>0) {
        if(isDir) {
            sfn = String("/")+fn;
        }
        strcpy(fn,sfn.c_str());
    } else {
        *fn='\0';
    }
    return LV_FS_RES_OK;
}

/**
 * Close the directory reading
 * @param drv pointer to a driver where this function belongs
 * @param dir_p pointer to an initialized 'DIR' or 'HANDLE' variable
 * @return LV_FS_RES_OK or any error from lv_fs_res_t enum
 */
static lv_fs_res_t sd_dir_close(lv_fs_drv_t * drv, void * dir_p)
{
    LV_UNUSED(drv);
    File& sdf = *(File*)dir_p;
    if(!sdf.isDirectory()) {
        return LV_FS_RES_INV_PARAM;
    }
    sdf.close();
    return LV_FS_RES_OK;
}


