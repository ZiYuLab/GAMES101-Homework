//
// Created by goksu on 4/6/19.
//

#include <algorithm>
#include <vector>
#include "rasterizer.hpp"
#include <opencv2/opencv.hpp>
#include <math.h>
#include <cstdio>

rst::pos_buf_id
rst::rasterizer::load_positions(const std::vector<Eigen::Vector3f>& positions)
{
  auto id = get_next_id();
  pos_buf.emplace(id, positions);

  return {id};
}

rst::ind_buf_id
rst::rasterizer::load_indices(const std::vector<Eigen::Vector3i>& indices)
{
  auto id = get_next_id();
  ind_buf.emplace(id, indices);

  return {id};
}
rst::col_buf_id
rst::rasterizer::load_colors(const std::vector<Eigen::Vector3f>& cols)
{
  auto id = get_next_id();
  col_buf.emplace(id, cols);

  return {id};
}

auto to_vec4(const Eigen::Vector3f& v3, float w = 1.0f)
{
  return Vector4f(v3.x(), v3.y(), v3.z(), w);
}

static bool
insideTriangle(float x, float y, const std::vector<Eigen::Vector3f>& vs)
{
  Eigen::Vector2f p(x, y);

  Eigen::Vector2f ap = p - vs[0].head<2>();
  Eigen::Vector2f bp = p - vs[1].head<2>();
  Eigen::Vector2f cp = p - vs[2].head<2>();
  Eigen::Vector2f ab = vs[1].head<2>() - vs[0].head<2>();
  Eigen::Vector2f bc = vs[2].head<2>() - vs[1].head<2>();
  Eigen::Vector2f ca = vs[0].head<2>() - vs[2].head<2>();

  auto sign = [](float n) {
    if (n > 0)
      return 1;
    else if (n < 0)
      return -1;
    else
      return 0;
  };

  int s = sign(ab.cross(ap));
  s = sign(bc.cross(bp)) == s ? s : 0;
  s = sign(ca.cross(cp)) == s ? s : 0;

  return s;
}

static std::tuple<float, float, float>
computeBarycentric2D(float x, float y, const Vector3f* v)
{
  float c1 =
      (x * (v[1].y() - v[2].y()) + (v[2].x() - v[1].x()) * y +
       v[1].x() * v[2].y() - v[2].x() * v[1].y()) /
      (v[0].x() * (v[1].y() - v[2].y()) + (v[2].x() - v[1].x()) * v[0].y() +
       v[1].x() * v[2].y() - v[2].x() * v[1].y());
  float c2 =
      (x * (v[2].y() - v[0].y()) + (v[0].x() - v[2].x()) * y +
       v[2].x() * v[0].y() - v[0].x() * v[2].y()) /
      (v[1].x() * (v[2].y() - v[0].y()) + (v[0].x() - v[2].x()) * v[1].y() +
       v[2].x() * v[0].y() - v[0].x() * v[2].y());
  float c3 =
      (x * (v[0].y() - v[1].y()) + (v[1].x() - v[0].x()) * y +
       v[0].x() * v[1].y() - v[1].x() * v[0].y()) /
      (v[2].x() * (v[0].y() - v[1].y()) + (v[1].x() - v[0].x()) * v[2].y() +
       v[0].x() * v[1].y() - v[1].x() * v[0].y());
  return {c1, c2, c3};
}

void rst::rasterizer::draw(pos_buf_id pos_buffer,
                           ind_buf_id ind_buffer,
                           col_buf_id col_buffer,
                           Primitive  type)
{
  auto& buf = pos_buf[pos_buffer.pos_id];
  auto& ind = ind_buf[ind_buffer.ind_id];
  auto& col = col_buf[col_buffer.col_id];

  float f1 = (50 - 0.1) / 2.0;
  float f2 = (50 + 0.1) / 2.0;

  Eigen::Matrix4f mvp = projection * view * model;
  for (auto& i : ind)
  {
    Triangle        t;
    Eigen::Vector4f v[] = {mvp * to_vec4(buf[i[0]], 1.0f),
                           mvp * to_vec4(buf[i[1]], 1.0f),
                           mvp * to_vec4(buf[i[2]], 1.0f)};
    // Homogeneous division
    for (auto& vec : v)
    {
      vec /= vec.w();
    }
    // Viewport transformation
    for (auto& vert : v)
    {
      vert.x() = 0.5 * width * (vert.x() + 1.0);
      vert.y() = 0.5 * height * (vert.y() + 1.0);
      vert.z() = vert.z() * f1 + f2;
    }

    for (int i = 0; i < 3; ++i)
    {
      t.setVertex(i, v[i].head<3>());
      t.setVertex(i, v[i].head<3>());
      t.setVertex(i, v[i].head<3>());
    }

    auto col_x = col[i[0]];
    auto col_y = col[i[1]];
    auto col_z = col[i[2]];

    t.setColor(0, col_x[0], col_x[1], col_x[2]);
    t.setColor(1, col_y[0], col_y[1], col_y[2]);
    t.setColor(2, col_z[0], col_z[1], col_z[2]);

    rasterize_triangle(t);
  }

  msaa_set_pixel();
}

// Screen space rasterization
void rst::rasterizer::rasterize_triangle(const Triangle& t)
{
  auto vs4 = t.toVector4();
  auto vs = std::vector<Eigen::Vector3f>(t.v, t.v + 3);

  // Build AABB for the triangle
  int min_x = std::max(0,
                       static_cast<int>(std::floor(
                           std::min({t.v[0].x(), t.v[1].x(), t.v[2].x()}))));
  int max_x = std::min(width,
                       static_cast<int>(std::ceil(
                           std::max({t.v[0].x(), t.v[1].x(), t.v[2].x()}))));

  int min_y = std::max(0,
                       static_cast<int>(std::floor(
                           std::min({t.v[0].y(), t.v[1].y(), t.v[2].y()}))));
  int max_y = std::min(height,
                       static_cast<int>(std::ceil(
                           std::max({t.v[0].y(), t.v[1].y(), t.v[2].y()}))));

  // 抄写
  auto get_deep = [&](float x, float y) {
    auto [alpha, beta, gamma] = computeBarycentric2D(x, y, t.v);
    float w_reciprocal =
        1.0 / (alpha / vs4[0].w() + beta / vs4[1].w() + gamma / vs4[2].w());
    float z_interpolated = alpha * vs4[0].z() / vs4[0].w() +
                           beta * vs4[1].z() / vs4[1].w() +
                           gamma * vs4[2].z() / vs4[2].w();
    z_interpolated *= w_reciprocal;
    return z_interpolated;
  };

  // For each pixel in the bounding box and rasterize the triangle

  // Each thread owns separate rows; triangles are still processed in order.
  #pragma omp parallel for schedule(static)
  for (int y = min_y; y < max_y; ++y)
  {
    for (int x = min_x; x < max_x; ++x)
    {
      // MSAA 4X

      size_t idx = get_index(x, y);
      for (int i = 0; i < 4; ++i)
      {
        const float px = x + (i % 2) * 0.5 + 0.25;
        const float py = y + (i / 2) * 0.5 + 0.25;

        if (insideTriangle(px, py, vs))
        {
          float deep = get_deep(px, py);
          if (deep < z_buf[idx].depths[i])
          {
            z_buf[idx].depths[i] = deep;
            z_buf[idx].colors[i] = t.getColor();
          }
        }
      }
    }
  }
}

void rst::rasterizer::msaa_set_pixel()
{
  for (int x = 0; x < width; ++x)
  {
    for (int y = 0; y < height; ++y)
    {
      size_t          idx = get_index(x, y);
      Eigen::Vector3f color(0, 0, 0);
      for (int i = 0; i < 4; ++i)
      {
        color += z_buf[idx].colors[i];
      }
      color /= 4.f;
      set_pixel(Eigen::Vector3f(x, y, 1), color);
    }
  }
}

void rst::rasterizer::set_model(const Eigen::Matrix4f& m) { model = m; }

void rst::rasterizer::set_view(const Eigen::Matrix4f& v) { view = v; }

void rst::rasterizer::set_projection(const Eigen::Matrix4f& p)
{
  projection = p;
}

void rst::rasterizer::clear(rst::Buffers buff)
{
  if ((buff & rst::Buffers::Color) == rst::Buffers::Color)
  {
    std::fill(frame_buf.begin(), frame_buf.end(), Eigen::Vector3f{0, 0, 0});
  }
  if ((buff & rst::Buffers::Depth) == rst::Buffers::Depth)
  {
    pixel_info default_pixel;
    default_pixel.colors.fill(Eigen::Vector3f{0, 0, 0});
    default_pixel.depths.fill(std::numeric_limits<float>::infinity());
    std::fill(z_buf.begin(), z_buf.end(), default_pixel);
  }
}

rst::rasterizer::rasterizer(int w, int h) : width(w), height(h)
{
  frame_buf.resize(w * h);
  z_buf.resize(w * h);
}

int rst::rasterizer::get_index(int x, int y)
{
  return (height - 1 - y) * width + x;
}

void rst::rasterizer::set_pixel(const Eigen::Vector3f& point,
                                const Eigen::Vector3f& color)
{
  // old index: auto ind = point.y() + point.x() * width;
  auto ind = (height - 1 - point.y()) * width + point.x();
  frame_buf[ind] = color;
}
