#include <libweston/libweston.h>

namespace hb {

Geometry::Geometry(int32_t x, int32_t y, int32_t width, int32_t height)
{
    this->_x = x;
    this->_y = y;
    this->_width = width;
    this->_height = height;
}

}
