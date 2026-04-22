#include "../../include/core/sds.hpp"

#include <limits>

//拿类型标志
static inline uint8_t sds_flags(const char *buf){
    return *(buf-1);
}

static void sds_set_header(void* sh, uint8_t type, size_t len, size_t alloc) {
    switch (type)
    {
        case SDS::TYPE_8:
        {
            auto* h = static_cast<SdsHdr8*>(sh);
            h->len = static_cast<uint8_t>(len);
            h->alloc = static_cast<uint8_t>(alloc);
            h->flags = type;
            break;
        }
        case SDS::TYPE_16:
        {
            auto* h = static_cast<SdsHdr16*>(sh);
            h->len = static_cast<uint16_t>(len);
            h->alloc = static_cast<uint16_t>(alloc);
            h->flags = type;
            break;
        }
        case SDS::TYPE_32:
        {
            auto* h = static_cast<SdsHdr32*>(sh);
            h->len = static_cast<uint32_t>(len);
            h->alloc = static_cast<uint32_t>(alloc);
            h->flags = type;
            break;
        }
        case SDS::TYPE_64:
        {
            auto* h = static_cast<SdsHdr64*>(sh);
            h->len = static_cast<uint64_t>(len);
            h->alloc = static_cast<uint64_t>(alloc);
            h->flags = type;
            break;
        }
    }
}

//选头部类型
uint8_t SDS::select_type(size_t len){
    if(len<=UINT8_MAX)return TYPE_8;    
    if(len<=UINT16_MAX)return TYPE_16;
    if(len<=UINT32_MAX)return TYPE_32;
    return TYPE_64;
}

//查头部大小
size_t SDS::hdr_size(uint8_t type){
    switch (type)
    {
        case TYPE_8:return sizeof(SdsHdr8);
        case TYPE_16:return sizeof(SdsHdr16);
        case TYPE_32:return sizeof(SdsHdr32);
        case TYPE_64:return sizeof(SdsHdr64);
    }
    return 0;
}
SDS::SDS():buf_(nullptr){
    init("",0);
}
SDS::SDS(const char* str){
    init(str,strlen(str));
}
SDS::SDS(std::string_view str){
    init(str.data(),str.size());
}

void SDS::init(const char*str,size_t len){
    uint8_t type=select_type(len);
    size_t hdrlen=hdr_size(type);

    void *sh=malloc(len+hdrlen+1);
    if(!sh)throw std::bad_alloc();
    buf_=(char*)sh+hdrlen;

    if (len > 0) {
        memcpy(buf_,str,len);
    }
    buf_[len]='\0';
    sds_set_header(sh, type, len, len);
}

void SDS::destroy(){
    if(!buf_)return ;
    uint8_t flags =sds_flags(buf_);
    uint8_t type =flags&TYPE_MASK;
    void *sh=buf_-hdr_size(type);
    free(sh);
    buf_=nullptr;
}

SDS::~SDS(){
    destroy();
}

SDS::SDS(SDS&& other) noexcept{
    buf_=other.buf_;
    other.buf_=nullptr;
}
SDS& SDS::operator=(SDS&& other) noexcept{
    if(this!=&other){
        destroy();
        buf_=other.buf_;
        other.buf_=nullptr;
    }
    return *this;
}
size_t SDS::len() const{
    uint8_t flags=sds_flags(buf_);
    uint8_t type =flags&TYPE_MASK;
    void* sh = (void*)(buf_ - hdr_size(type));
    switch (type)
    {
        case TYPE_8:return ((SdsHdr8*)sh)->len;
        case TYPE_16:return ((SdsHdr16*)sh)->len;
        case TYPE_32:return ((SdsHdr32*)sh)->len;
        case TYPE_64:return ((SdsHdr64*)sh)->len;
    }
    return 0;
}
size_t SDS::capacity() const{
    uint8_t flags=sds_flags(buf_);
    uint8_t type =flags&TYPE_MASK;
    void* sh = (void*)(buf_ - hdr_size(type));
    switch (type)
    {
        case TYPE_8:return ((SdsHdr8*)sh)->alloc;
        case TYPE_16:return ((SdsHdr16*)sh)->alloc;
        case TYPE_32:return ((SdsHdr32*)sh)->alloc;
        case TYPE_64:return ((SdsHdr64*)sh)->alloc;
    }
    return 0;
}
size_t SDS::avail() const{
    return capacity()-len();
}

void SDS::setLen(size_t newlen){
    uint8_t flags=sds_flags(buf_);
    uint8_t type =flags&TYPE_MASK;
    switch (type)
    {
        case TYPE_8:((SdsHdr8*)(buf_-sizeof(SdsHdr8)))->len=newlen;break;
        case TYPE_16:((SdsHdr16*)(buf_-sizeof(SdsHdr16)))->len=newlen;break;
        case TYPE_32:((SdsHdr32*)(buf_-sizeof(SdsHdr32)))->len=newlen;break;
        case TYPE_64:((SdsHdr64*)(buf_-sizeof(SdsHdr64)))->len=newlen;break;
    }
}

void SDS::makeRoomFor(size_t addlen){
    size_t len=this->len();
    size_t alloc =capacity();
    if(alloc-len>=addlen)return ;
    if (addlen > std::numeric_limits<size_t>::max() - len) {
        throw std::length_error("SDS length overflow");
    }
    size_t newlen=len+addlen;
    size_t newalloc;

    if(newlen<MAX_PREALLOC){
        newalloc=newlen*2;
    }else{
        newalloc=newlen+MAX_PREALLOC;
    }
    uint8_t type=select_type(newalloc);
    size_t hdrlen = hdr_size(type);

    const uint8_t oldtype = sds_flags(buf_) & TYPE_MASK;
    const size_t oldhdrlen = hdr_size(oldtype);
    void* oldsh = buf_ - oldhdrlen;
    void* newsh = nullptr;

    if (oldtype == type) {
        newsh = realloc(oldsh, hdrlen + newalloc + 1);
        if(!newsh)
            throw std::bad_alloc();
    } else {
        newsh = malloc(hdrlen + newalloc + 1);
        if(!newsh)
            throw std::bad_alloc();
        char* newbuf = static_cast<char*>(newsh) + hdrlen;
        memcpy(newbuf, buf_, len + 1);
        free(oldsh);
    }

    buf_ = static_cast<char*>(newsh) + hdrlen;
    sds_set_header(newsh, type, len, newalloc);
}
void SDS::append(const char* str, size_t addlen){
    size_t curlen = len();
    makeRoomFor(addlen);
    memcpy(buf_ + curlen,str,addlen);
    setLen(curlen + addlen);
    buf_[curlen + addlen] = '\0';
}
void SDS::append(std::string_view str){
    append(str.data(),str.size());
}

const char* SDS::c_str() const
{
    return buf_;
}

void SDS::clear(){
    setLen(0);
    buf_[0]='\0';
}
