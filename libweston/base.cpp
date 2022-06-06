#include <libweston/libweston.h>

namespace hb {

Geometry::Geometry()
{
    this->_x = 0;
    this->_y = 0;
    this->_width = 0;
    this->_height = 0;
}

Geometry::Geometry(int32_t x, int32_t y, int32_t width, int32_t height)
{
    this->_x = x;
    this->_y = y;
    this->_width = width;
    this->_height = height;
}

int32_t Geometry::x() const
{
    return this->_x;
}

void Geometry::set_x(int32_t x)
{
    if (this->_x != x) {
        this->_x = x;
    }
}

int32_t Geometry::y() const
{
    return this->_y;
}

void Geometry::set_y(int32_t y)
{
    if (this->_y != y) {
        this->_y = y;
    }
}

int32_t Geometry::width() const
{
    return this->_width;
}

void Geometry::set_width(int32_t width)
{
    if (this->_width != width) {
        this->_width = width;
    }
}

int32_t Geometry::height() const
{
    return this->_height;
}

void Geometry::set_height(int32_t height)
{
    if (this->_height != height) {
        this->_height = height;
    }
}

} // namespace hb
