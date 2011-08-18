/****************************************************************************
**
** Copyright (c) 2011 libmv authors.
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to
** deal in the Software without restriction, including without limitation the
** rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
** sell copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
** IN THE SOFTWARE.
**
****************************************************************************/

#include "ui/tracker/tracker.h"
#include "ui/tracker/scene.h"
#include "ui/tracker/gl.h"

#include <QMouseEvent>
#include <QFileInfo>
#include <QDebug>
#include <QTime>

#if (QT_VERSION < QT_VERSION_CHECK(4, 7, 0))
#define constBits bits
#endif

using libmv::mat32;
inline vec2 operator*(mat32 m, vec2 v) {
  return vec2(v.x*m(0,0)+v.y*m(0,1)+m(0,2),v.x*m(1,0)+v.y*m(1,1)+m(1,2));
}

QDataStream& operator<<(QDataStream& s, const mat32& m) { s.writeRawData((char*)&m,sizeof(m)); return s; }
QDataStream& operator>>(QDataStream& s, mat32& m) { s.readRawData((char*)&m,sizeof(m)); return s; }
QDebug operator <<(QDebug d, mat32 m) {
  return d.nospace()<<m(0,0)<<", "<<m(0,1)<<", "<<m(0,2)<<"\n"<<m(1,0)<<", "<<m(1,1)<<", "<<m(1,2);
}

Tracker::Tracker(libmv::CameraIntrinsics* intrinsics) : intrinsics_(intrinsics),
    scene_(0), undistort_(false),
    current_(0), active_track_(-1), dragged_(false) {
  setMinimumHeight(64);
}

void Tracker::Load(QString path) {
  QFile file(path + (QFileInfo(path).isDir()?"/":".") + "tracks");
  if (file.open(QFile::ReadOnly)) {
    QDataStream s(&file);
    s >> tracks;
  }
  emit trackChanged(selected_tracks_);
}

void Tracker::Save(QString path) {
  QFile file(path + (QFileInfo(path).isDir()?"/":".") + "tracks");
  if (file.open(QFile::WriteOnly|QFile::Truncate)) {
    QDataStream s(&file);
    s << tracks;
  }
}

inline float sqr(float x) { return x*x; }
void Tracker::SetImage(int id, QImage image) {
  makeCurrent();
  current_ = id;
  image_ = image;
#ifdef LENS_DISTORTION
  if(undistort_) {
    QTime time; time.start();
    QImage correct(image.width(),image.height(),QImage::Format_Indexed8);
    intrinsics_->Undistort(image.constBits(), correct.bits(), image.width(), image.height(), 1);
    qDebug() << QString("%1x%2 image warped in %3 ms")
                .arg(image.width()).arg(image.height()).arg(time.elapsed());
    image_.upload(correct);
  } else
#endif
  {
    texture_.upload(image);
  }
  upload();
  emit trackChanged(selected_tracks_);
}

void Tracker::SetUndistort(bool undistort) {
  undistort_ = undistort;
}

void Tracker::SetOverlay(Scene* scene) {
  scene_ = scene;
}

void Tracker::Track(int previous, int next, QImage search) {
  QTime time; time.start();
  int width = search.width(), height = search.height(), stride = search.bytesPerLine();
  foreach(int i, selected_tracks_) {
    QVector<mat32>& track = tracks[i];
    mat32 marker = track[previous];

    // Stop tracking near borders
    int x = marker(0,2), y = marker(1,2);
    if( x < kPatternSize || y < kPatternSize ||
        x >= width-kPatternSize || y >= height-kPatternSize ) {
      continue;
    }

    // Compute clipped search region
    int x0 = qMax( x - kSearchSize/2, 0     );
    int y0 = qMax( y - kSearchSize/2, 0     );
    int x1 = qMin( x + kSearchSize/2, width );
    int y1 = qMin( y + kSearchSize/2, height);
    int w = x1-x0, h = y1-y0;

    // Translate to search region
    marker(0,2) -= x0, marker(1,2) -= y0;

    qDebug() << libmv::Track(references[i].data, (ubyte*)search.constBits()+y0*stride+x0, stride, w, h, &marker);

    // Translate back to image
    marker(0,2) += x0, marker(1,2) += y0;

    if(track.count()<=next) track.resize(next+1);
    track[next] = marker;
  }
  last_frame = next;
  qDebug() << selected_tracks_.size() <<"markers in" << time.elapsed() << "ms";
}

void Tracker::select(QVector<int> tracks) {
  selected_tracks_ = tracks;
  upload();
}

void Tracker::deleteSelectedMarkers() {
  foreach (int track, selected_tracks_) {
    tracks[track][current_] = mat32();
  }
  selected_tracks_.clear();
  upload();
  emit trackChanged(selected_tracks_);
}

void Tracker::deleteSelectedTracks() {
  foreach (int track, selected_tracks_) {
    tracks.remove(track);
  }
  selected_tracks_.clear();
  upload();
  emit trackChanged(selected_tracks_);
}

void Tracker::DrawMarker(const mat32& marker, QVector<vec2> *lines) {
  vec2 quad[] = { vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, 1) };
  for (int i = 0; i < 4; i++) {
    *lines << marker*((kSearchSize/2)*quad[i]);
    *lines << marker*((kSearchSize/2)*quad[(i+1)%4]);
  }
  for (int i = 0; i < 4; i++) {
    *lines << marker*((kPatternSize/2)*quad[i]);
    *lines << marker*((kPatternSize/2)*quad[(i+1)%4]);
  }
}

void Tracker::upload() {
  makeCurrent();
  QVector<vec2> lines;
  foreach(QVector<mat32> track, tracks) {
    if (current_ < track.count()) {
      const mat32 current = track[current_];
      if(current) DrawMarker(current, &lines);
    }
  }
  foreach (int index, selected_tracks_) {
    QVector<mat32> track = tracks[index];
    if (current_ >= track.count()) {
      selected_tracks_.remove(selected_tracks_.indexOf(index));
      continue;
    }
    for (int i = 0; i < track.size()-1; i++) {
      lines << track[i]*vec2(0,0) <<  track[i+1]*vec2(0,0);
    }
    const mat32 current = track[current_];
    DrawMarker(current, &lines);
    DrawMarker(current, &lines);
    DrawMarker(current, &lines);
  }
  markers_.primitiveType = 2;
  markers_.upload(lines.constData(), lines.count(), sizeof(vec2));
  update();
}

void Tracker::Render(int x, int y, int w, int h, int image, int track) {
  glBindWindow(x, y, w, h, false);
  glDisableBlend();
  static GLShader image_shader;
  if (!image_shader.id) {
    image_shader.compile(glsl("vertex image"), glsl("fragment image"));
  }
  image_shader.bind();
  image_shader["image"] = 0;
  texture_.bind(0);
  mat4 transform;
  if (image >= 0 && track >= 0) {
    //mat3 marker = tracks[track][image];
    /*vec2 center(marker.x, marker.y);
    vec2 min = (center-kSearchSize/2);
    vec2 max = (center+kSearchSize/2);
    vec2 size( image_.width, image_.height );
    glQuad(vec4(-1, 1, min/size), vec4(1, -1, max/size));
    transform.scale(vec3(1, -1, 0));
    transform.translate(vec3(-1, -1, 0));
    transform.scale(vec3(2/(max-min), 1));
    transform.translate(vec3(-min, 0));*/
  } else {
    float width = 0, height = 0;
    int W = intrinsics_->image_width(), H = intrinsics_->image_height();
    if (W*h > H*w) {
      width = 1;
      height = static_cast<float>(H*w)/(W*h);
    } else {
      height = 1;
      width = static_cast<float>(W*h)/(H*w);
    }
    glQuad(vec4(-width, -height, 0, 1), vec4(width, height, 1, 0));
    //if (scene_ && scene_->isVisible()) scene_->Render(w, h, current_);
    W = image_.width(), H = image_.height();
    transform.scale(vec3(2*width/W, -2*height/H, 1));
    transform.translate(vec3(-W/2, -H/2, 0));
    transform_ = transform;
  }
  static GLShader marker_shader;
  if (!marker_shader.id) {
    marker_shader.compile(glsl("vertex transform marker"),
                          glsl("fragment transform marker"));
  }
  marker_shader.bind();
  marker_shader["transform"] = transform;
  markers_.bind();
  markers_.bindAttribute(&marker_shader, "position", 2);
  glAdditiveBlendMode();
  markers_.draw();
}

void Tracker::paintGL() {
  glBindWindow(0, 0, width(), height(), true);
  Render(0, 0, width(), height());
}

void Tracker::mousePressEvent(QMouseEvent* e) {
  vec2 pos = transform_.inverse()*vec2(2.0*e->x()/width()-1,
                                       1-2.0*e->y()/height());
  last_position_ = pos;
  int i=0;
  foreach(QVector<mat32> track, tracks) {
    mat32 marker = track[current_];
    vec2 center = vec2(marker(0,2), marker(1,2));
    if (pos > center-kSearchSize/2 && pos < center+kSearchSize/2) {
      active_track_ = i;
      return;
    }
    i++;
  }
  int new_track = tracks.count();
  mat32 marker;
  marker(0,2) = pos.x, marker(1,2) = pos.y;
  tracks << QVector<mat32>(1,marker);
  Pattern pattern;
  libmv::SamplePattern((ubyte*)image_.constBits(),image_.bytesPerLine(),marker,pattern.data);
  references << pattern;
  selected_tracks_ += new_track;
  active_track_ = new_track;
  emit trackChanged(selected_tracks_);
  upload();
}

void Tracker::mouseMoveEvent(QMouseEvent* e) {
  vec2 pos = transform_.inverse()*vec2(2.0*e->x()/width()-1,
                                       1-2.0*e->y()/height());
  vec2 delta = pos-last_position_;
  mat32& marker = tracks[active_track_][current_];
  marker(0,2) += delta.x;
  marker(1,2) += delta.y;
  libmv::SamplePattern((ubyte*)image_.constBits(),image_.bytesPerLine(),marker,references[active_track_].data);
  upload();
  last_position_ = pos;
  dragged_ = true;
  emit trackChanged(selected_tracks_);
}

void Tracker::mouseReleaseEvent(QMouseEvent */*event*/) {
  if (!dragged_ && active_track_ >= 0) {
    if (selected_tracks_.contains(active_track_)) {
      selected_tracks_.remove(selected_tracks_.indexOf(active_track_));
    } else {
      selected_tracks_ << active_track_;
    }
    emit trackChanged(selected_tracks_);
  }
  active_track_ = -1;
  dragged_ = false;
  upload();
}

