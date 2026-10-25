// SPDX-License-Identifier: GPL-3.0-or-later
// Uses Comic Chat XR's export approach: textured geometry, embedded PNG,
// KHR_materials_unlit, aligned glTF accessors, and an atomic single-file save.
#include "gltf-export.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <glib.h>
#include <limits>
#include <queue>
#include <nlohmann/json.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <unistd.h>
#include <vector>
namespace qimgvr {
namespace {
using Json = nlohmann::json;
struct Vertex { float x,y,z,nx=0,ny=0,nz=1,u,v; };
void u32(std::vector<unsigned char> &b, uint32_t n) {
  for (int shift : {0,8,16,24}) b.push_back((n>>shift)&255);
}
void f32(std::vector<unsigned char> &b, float value) {
  uint32_t bits; static_assert(sizeof(bits)==sizeof(value));
  std::memcpy(&bits,&value,4); u32(b,bits);
}
void saveAtomic(const std::filesystem::path &path, const std::vector<unsigned char> &bytes) {
  auto parent=path.parent_path(); if(parent.empty()) parent=".";
  std::string pattern=(parent/("."+path.filename().string()+".qimgvr-XXXXXX")).string();
  std::vector<char> name(pattern.begin(),pattern.end()); name.push_back(0);
  int fd=mkstemp(name.data());
  if(fd<0) throw std::runtime_error("Cannot create glTF export in "+parent.string());
  try {
    size_t done=0;
    while(done<bytes.size()) {
      auto n=write(fd,bytes.data()+done,bytes.size()-done);
      if(n<0 && errno==EINTR) continue;
      if(n<=0) throw std::runtime_error("Cannot write glTF export");
      done+=size_t(n);
    }
    if(fsync(fd)) throw std::runtime_error("Cannot flush glTF export");
    int status=close(fd); fd=-1;
    if(status) throw std::runtime_error("Cannot close glTF export");
    std::filesystem::rename(name.data(),path);
  } catch(...) {
    if(fd>=0) close(fd);
    unlink(name.data()); throw;
  }
}
struct DepthField {
  cv::Mat disparity;
  double matchedFraction=0;
};
// An invalid stereo match is missing information, not zero disparity. Validate
// in both directions, then extend confident depth along paths which prefer
// staying inside the same colour region. Transparent texels are barriers.
DepthField reconstruct(const cv::Mat &left,const cv::Mat &right,
                       const cv::Mat &alpha,const cv::Mat &rightAlpha) {
  const int width=left.cols,height=left.rows;
  DepthField field{cv::Mat::zeros(left.size(),CV_32F),0};
  if(width<64 || height<8)return field;
  int range=std::max(16,int(std::ceil(width*.125/16))*16),minimum=-range;
  // Give SGBM room for its search at both image edges; confidence tests below
  // still require a correspondence inside the actual, unpadded right image.
  cv::Mat l,r,raw,reverse;
  cv::copyMakeBorder(left,l,0,0,range+8,range+8,cv::BORDER_REPLICATE);
  cv::copyMakeBorder(right,r,0,0,range+8,range+8,cv::BORDER_REPLICATE);
  auto matcher=cv::StereoSGBM::create(minimum,2*range,5,8*25,32*25,1,31,8,50,2,cv::StereoSGBM::MODE_SGBM_3WAY);
  matcher->compute(l,r,raw);matcher->compute(r,l,reverse);
  cv::Mat valid=cv::Mat::zeros(left.size(),CV_8U),gradientX,gradientY,texture;
  cv::Sobel(left,gradientX,CV_32F,1,0);cv::Sobel(left,gradientY,CV_32F,0,1);
  texture=cv::abs(gradientX)+cv::abs(gradientY);
  cv::boxFilter(texture,texture,-1,cv::Size(5,5));
  size_t covered=0,matched=0;
  for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
    if(!alpha.at<unsigned char>(y,x))continue;
    ++covered;
    float d=raw.at<int16_t>(y,x+range+8)/16.f;
    int other=int(std::lround(x-d));
    if(d<=minimum-1 || other<0 || other>=width || !rightAlpha.at<unsigned char>(y,other))continue;
    float back=reverse.at<int16_t>(y,other+range+8)/16.f;
    if(back<=minimum-1 || std::abs(d+back)>1.25f || texture.at<float>(y,x)<12)continue;
    field.disparity.at<float>(y,x)=d;valid.at<unsigned char>(y,x)=255;++matched;
  }
  field.matchedFraction=covered?double(matched)/covered:0;
  if(field.matchedFraction<.02){field.disparity.setTo(0);return field;}
  // Reject isolated residual errors without averaging foreground and background.
  cv::Mat seeds=field.disparity.clone();
  for(int y=0;y<height;++y)for(int x=0;x<width;++x)if(valid.at<unsigned char>(y,x)) {
    std::array<float,25> samples;int n=0;
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
      int a=x+dx,b=y+dy;
      if(a>=0 && b>=0 && a<width && b<height && valid.at<unsigned char>(b,a))samples[n++]=field.disparity.at<float>(b,a);
    }
    if(n>=5) {
      std::nth_element(samples.begin(),samples.begin()+n/2,samples.begin()+n);
      if(std::abs(seeds.at<float>(y,x)-samples[n/2])>2.5f)valid.at<unsigned char>(y,x)=0;
      else seeds.at<float>(y,x)=samples[n/2];
    }
  }
  struct Candidate {
    float cost;int pixel;
    bool operator<(const Candidate &b)const{return cost>b.cost;}
  };
  std::priority_queue<Candidate> frontier;
  cv::Mat costs(left.size(),CV_32F,cv::Scalar(INFINITY));
  field.disparity.setTo(0);
  size_t seedCount=0;
  for(int y=0;y<height;++y)for(int x=0;x<width;++x)if(valid.at<unsigned char>(y,x)) {
    costs.at<float>(y,x)=0;field.disparity.at<float>(y,x)=seeds.at<float>(y,x);frontier.push({0,y*width+x});++seedCount;
  }
  field.matchedFraction=covered?double(seedCount)/covered:0;
  if(field.matchedFraction<.02){field.disparity.setTo(0);return field;}
  while(!frontier.empty()) {
    auto current=frontier.top();frontier.pop();
    int x=current.pixel%width,y=current.pixel/width;
    if(current.cost!=costs.at<float>(y,x))continue;
    for(auto delta:std::array<std::pair<int,int>,4>{{{-1,0},{1,0},{0,-1},{0,1}}}) {
      int a=x+delta.first,b=y+delta.second;
      if(a<0 || b<0 || a>=width || b>=height || !alpha.at<unsigned char>(b,a))continue;
      float edge=std::abs(int(left.at<unsigned char>(y,x))-int(left.at<unsigned char>(b,a)))/16.f;
      float cost=current.cost+1+edge*edge;
      if(cost<costs.at<float>(b,a)) {
        costs.at<float>(b,a)=cost;field.disparity.at<float>(b,a)=field.disparity.at<float>(y,x);
        frontier.push({cost,b*width+a});
      }
    }
  }
  // Smooth only within a small disparity band. Do not replace missing samples
  // with zero or blur real silhouette discontinuities into sloping curtains.
  cv::Mat smooth=field.disparity.clone();
  for(int y=0;y<height;++y)for(int x=0;x<width;++x)if(alpha.at<unsigned char>(y,x)) {
    float d=field.disparity.at<float>(y,x),sum=0,weight=0;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
      int a=x+dx,b=y+dy;if(a<0 || b<0 || a>=width || b>=height || !alpha.at<unsigned char>(b,a))continue;
      float v=field.disparity.at<float>(b,a);
      if(std::abs(v-d)>1.5f)continue;
      float colour=std::abs(int(left.at<unsigned char>(y,x))-int(left.at<unsigned char>(b,a)))/12.f;
      float w=1.f/(1+colour*colour+dx*dx+dy*dy);sum+=w*v;weight+=w;
    }
    if(weight)smooth.at<float>(y,x)=sum/weight;
  }
  field.disparity=std::move(smooth);return field;
}

}
std::string GltfExportResult::summary() const {
  return "Exported glTF: "+std::to_string(triangles)+" triangles"+
      (flatFallback ? " (flat fallback: insufficient stereo matches)" : " (approximate stereo relief)");
}
GltfExportResult exportStereoGltf(const cv::Mat &rgba, const std::filesystem::path &path,
                                const std::string &sourceName) {
  auto extension=path.extension().string();
  std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return std::tolower(c);});
  if(extension!=".glb" && extension!=".gltf") throw std::runtime_error("Choose a .glb or .gltf export filename");
  if(rgba.empty() || rgba.type()!=CV_8UC4 || rgba.cols%2 || rgba.cols<4 || rgba.rows<2)
    throw std::runtime_error("Export requires an even-width full SBS RGBA still image");
  const int eyeWidth=rgba.cols/2;
  cv::Mat left=rgba(cv::Rect(0,0,eyeWidth,rgba.rows)), right=rgba(cv::Rect(eyeWidth,0,eyeWidth,rgba.rows));
  // Bound matching cost independently of texture resolution and mesh density.
  double scale=std::min(1.,1024./std::max(left.cols,left.rows));
  cv::Size size(std::max(1,int(std::lround(left.cols*scale))),std::max(1,int(std::lround(left.rows*scale))));
  cv::Mat l,r,alpha,rightAlpha,small;
  cv::resize(left,small,size,0,0,cv::INTER_AREA);
  cv::cvtColor(small,l,cv::COLOR_RGBA2GRAY); cv::extractChannel(small,alpha,3);
  cv::resize(right,small,size,0,0,cv::INTER_AREA); cv::cvtColor(small,r,cv::COLOR_RGBA2GRAY);
  cv::extractChannel(small,rightAlpha,3);
  for(int y=0;y<size.height;++y) for(int x=0;x<size.width;++x) {
    l.at<unsigned char>(y,x)=int(l.at<unsigned char>(y,x))*alpha.at<unsigned char>(y,x)/255;
    r.at<unsigned char>(y,x)=int(r.at<unsigned char>(y,x))*rightAlpha.at<unsigned char>(y,x)/255;
  }
  auto reconstructed=reconstruct(l,r,alpha,rightAlpha);
  auto &disparity=reconstructed.disparity;
  GltfExportResult result;
  result.matchedFraction=reconstructed.matchedFraction;
  result.flatFallback=result.matchedFraction<.02;
  constexpr float width=1.3f, distance=1.25f, baseline=.064f;
  const float height=width*left.rows/left.cols;
  double meshScale=std::min(1.,512./std::max(size.width,size.height));
  int columns=std::max(2,int(std::lround(size.width*meshScale))),
      rows=std::max(2,int(std::lround(size.height*meshScale)));
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  vertices.reserve(size_t(columns)*rows);
  for(int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
    float u=float(x)/(columns-1),v=float(y)/(rows-1);
    int px=std::min(size.width-1,int(std::lround(u*(size.width-1))));
    int py=std::min(size.height-1,int(std::lround(v*(size.height-1))));
    float d=disparity.at<float>(py,px)*width/size.width;
    float z=std::clamp(baseline*distance/std::max(baseline+d,baseline/2),distance*.5f,distance*2.f);
    // Left-camera backprojection: positive crossed disparity comes forward.
    vertices.push_back({((u-.5f)*width+baseline/2)*z/distance-baseline/2,
                        (.5f-v)*height*z/distance,distance-z,0,0,0,u,v});
  }
  auto triangle=[&](uint32_t a,uint32_t b,uint32_t c) {
    auto &p=vertices[a], &q=vertices[b], &r=vertices[c];
    auto opaque=[&](const Vertex &v) {
      return left.at<cv::Vec4b>(std::min(left.rows-1,int(v.v*(left.rows-1))),std::min(left.cols-1,int(v.u*(left.cols-1))))[3]>0;
    };
    if(!opaque(p) && !opaque(q) && !opaque(r)) return;
    // Do not stretch triangles across foreground/background discontinuities.
    if(std::max({p.z,q.z,r.z})-std::min({p.z,q.z,r.z})>.15f) return;
    indices.insert(indices.end(),{a,b,c});
    float ax=q.x-p.x,ay=q.y-p.y,az=q.z-p.z,bx=r.x-p.x,by=r.y-p.y,bz=r.z-p.z;
    float nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx;
    for(auto i:{a,b,c}) {vertices[i].nx+=nx; vertices[i].ny+=ny; vertices[i].nz+=nz;}
  };
  for(int y=0;y<rows-1;++y) for(int x=0;x<columns-1;++x) {
    uint32_t a=y*columns+x,b=a+columns;
    triangle(a,b,b+1); triangle(a,b+1,a+1);
  }
  if(indices.empty()) throw std::runtime_error("No visible stereo surface to export");
  for(auto &v:vertices) {
    float length=std::sqrt(v.nx*v.nx+v.ny*v.ny+v.nz*v.nz);
    if(length>0) {v.nx/=length;v.ny/=length;v.nz/=length;} else v.nz=1;
  }
  std::vector<unsigned char> buffer;
  std::array<float,3> minimum{INFINITY,INFINITY,INFINITY},maximum{-INFINITY,-INFINITY,-INFINITY};
  for(auto &v:vertices) {
    for(auto value:{v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v}) f32(buffer,value);
    for(int c=0;c<3;++c) {float value=std::array<float,3>{v.x,v.y,v.z}[c];minimum[c]=std::min(minimum[c],value);maximum[c]=std::max(maximum[c],value);}
  }
  size_t indexOffset=buffer.size(); for(auto i:indices) u32(buffer,i);
  cv::Mat bgra; cv::cvtColor(left,bgra,cv::COLOR_RGBA2BGRA);
  std::vector<unsigned char> png;
  if(!cv::imencode(".png",bgra,png)) throw std::runtime_error("Cannot encode glTF texture");
  size_t pngOffset=buffer.size(); buffer.insert(buffer.end(),png.begin(),png.end());
  while(buffer.size()%4) buffer.push_back(0);
  cv::Mat sourceAlpha;cv::extractChannel(left,sourceAlpha,3);
  cv::Mat partial=(sourceAlpha>0) & (sourceAlpha<255);
  const char *alphaMode=cv::countNonZero(partial)?"BLEND":cv::countNonZero(sourceAlpha<255)?"MASK":"OPAQUE";
  Json extras={{"source",std::filesystem::path(sourceName).filename().string()},
               {"representation","approximate-stereo-relief"},{"inputLayout","full-sbs-left-right"},
               {"eyeSize",{left.cols,left.rows}},{"matchedFraction",result.matchedFraction},
               {"flatFallback",result.flatFallback},{"assumedBaselineMetres",baseline},
               {"assumedConvergenceMetres",distance},{"screenWidthMetres",width},
               {"unmatchedDepth","edge-aware-confident-neighbour"},{"matching","bidirectional-consistency"},{"depthDiscontinuityMetres",.15}};
  Json j={{"asset",{{"version","2.0"},{"generator","de-xr qimgvr prototype"}}},
    {"extensionsUsed",{"KHR_materials_unlit"}}, {"scene",0},
    {"scenes",Json::array({{{"nodes",{0}}}})},
    {"nodes",Json::array({{{"name","Stereo image relief"},{"mesh",0},{"extras",extras}}})},
    {"meshes",Json::array({{{"primitives",Json::array({{{"attributes",{{"POSITION",0},{"NORMAL",1},{"TEXCOORD_0",2}}},{"indices",3},{"material",0}}})}}})},
    {"materials",Json::array({{{"name","Stereo image"},{"pbrMetallicRoughness",{{"baseColorTexture",{{"index",0}}},{"metallicFactor",0},{"roughnessFactor",1}}},
      {"extensions",{{"KHR_materials_unlit",Json::object()}}},{"alphaMode",alphaMode},{"doubleSided",true}}})},
    {"samplers",Json::array({{{"magFilter",9729},{"minFilter",9729},{"wrapS",33071},{"wrapT",33071}}})},
    {"textures",Json::array({{{"source",0},{"sampler",0}}})},
    {"images",Json::array({{{"bufferView",2},{"mimeType","image/png"}}})},
    {"bufferViews",Json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",indexOffset},{"byteStride",32},{"target",34962}},
      {{"buffer",0},{"byteOffset",indexOffset},{"byteLength",indices.size()*4},{"target",34963}},
      {{"buffer",0},{"byteOffset",pngOffset},{"byteLength",png.size()}}})},
    {"accessors",Json::array({{{"bufferView",0},{"byteOffset",0},{"componentType",5126},{"count",vertices.size()},{"type","VEC3"},{"min",minimum},{"max",maximum}},
      {{"bufferView",0},{"byteOffset",12},{"componentType",5126},{"count",vertices.size()},{"type","VEC3"}},
      {{"bufferView",0},{"byteOffset",24},{"componentType",5126},{"count",vertices.size()},{"type","VEC2"}},
      {{"bufferView",1},{"componentType",5125},{"count",indices.size()},{"type","SCALAR"}}})},
    {"buffers",Json::array({{{"byteLength",buffer.size()}}})}};
  std::vector<unsigned char> file;
  if(extension==".gltf") {
    auto encoded=g_base64_encode(buffer.data(),buffer.size());
    j["buffers"][0]["uri"]="data:application/octet-stream;base64,"+std::string(encoded);g_free(encoded);
    auto text=j.dump(2);file.assign(text.begin(),text.end());file.push_back('\n');
  } else {
    auto text=j.dump();while(text.size()%4) text+=' ';
    uint64_t total=uint64_t(28)+text.size()+buffer.size();
    if(total>std::numeric_limits<uint32_t>::max()) throw std::runtime_error("glTF export exceeds the GLB size limit");
    u32(file,0x46546c67);u32(file,2);u32(file,total);u32(file,text.size());u32(file,0x4e4f534a);
    file.insert(file.end(),text.begin(),text.end());u32(file,buffer.size());u32(file,0x004e4942);
    file.insert(file.end(),buffer.begin(),buffer.end());
  }
  saveAtomic(path,file);
  result.vertices=vertices.size();result.triangles=indices.size()/3;return result;
}
}
