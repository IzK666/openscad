#include <boost/foreach.hpp>
#include <boost/regex.hpp>
#include <boost/filesystem.hpp>
#include <sstream>
#include <iostream>
#include <fstream>
#include <locale.h>

#include "cgalutils.h"
#include "export.h"
#include "polyset.h"
#include "CGAL_Nef_polyhedron.h"

#pragma push_macro("NDEBUG")
#undef NDEBUG
#include <CGAL/IO/Nef_polyhedron_iostream_3.h>
#pragma pop_macro("NDEBUG")

using namespace CGALUtils;
namespace fs=boost::filesystem;

#define STL_FACET_NUMBYTES 4*3*4+2
// as there is no 'float32_t' standard, we assume the systems 'float'
// is a 'binary32' aka 'single' standard IEEE 32-bit floating point type
union stl_facet {
	uint8_t data8[ STL_FACET_NUMBYTES ];
	uint32_t data32[4*3];
	struct facet_data {
	  float i, j, k;
	  float x1, y1, z1;
	  float x2, y2, z2;
	  float x3, y3, z3;
	  uint16_t attribute_byte_count;
	} data;
};

void uint32_byte_swap( uint32_t &x )
{
#if __GNUC__ >= 4 && __GNUC_MINOR__ >= 3
	x = __builtin_bswap32( x );
#elif defined(__clang__)
	x = __builtin_bswap32( x );
#elif defined(_MSC_VER)
	x = _byteswap_ulong( x );
#else
	uint32_t b1 = ( 0x000000FF & x ) << 24;
	uint32_t b2 = ( 0x0000FF00 & x ) << 8;
	uint32_t b3 = ( 0x00FF0000 & x ) >> 8;
	uint32_t b4 = ( 0xFF000000 & x ) >> 24;
	x = b1 | b2 | b3 | b4;
#endif
}

void read_stl_facet( std::ifstream &f, stl_facet &facet )
{
	f.read( (char*)facet.data8, STL_FACET_NUMBYTES );
#ifdef BOOST_BIG_ENDIAN
	for ( int i = 0; i < 12; ++i ) {
		uint32_byte_swap( facet.data32[ i ] );
	}
	// we ignore attribute byte count
#endif
}

PolySet *import_stl(const std::string &filename)
{
  PolySet *p = new PolySet(3);

  // Open file and position at the end
  std::ifstream f(filename.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
  if (!f.good()) {
    LOG(message_group::Warning,Location::None,"","Can't open import file: %1$s",filename);
    return NULL;
  }

  boost::regex ex_sfe("solid|facet|endloop");
  boost::regex ex_outer("outer loop");
  boost::regex ex_vertex("vertex");
  boost::regex ex_vertices("\\s*vertex\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)");

  bool binary = false;
  std::streampos file_size = f.tellg();
  f.seekg(80);
  if (f.good() && !f.eof()) {
    uint32_t facenum = 0;
    f.read((char *)&facenum, sizeof(uint32_t));
#ifdef BOOST_BIG_ENDIAN
    uint32_byte_swap( facenum );
#endif
    if (file_size ==  static_cast<std::streamoff>(80 + 4 + 50*facenum)) {
      binary = true;
    }
  }
  f.seekg(0);

  char data[5];
  f.read(data, 5);
  if (!binary && !f.eof() && f.good() && !memcmp(data, "solid", 5)) {
    int i = 0;
    double vdata[3][3];
    std::string line;
    std::getline(f, line);
    while (!f.eof()) {
      std::getline(f, line);
      boost::trim(line);
      if (boost::regex_search(line, ex_sfe)) {
        continue;
      }
      if (boost::regex_search(line, ex_outer)) {
        i = 0;
        continue;
      }
      boost::smatch results;
      if (boost::regex_search(line, results, ex_vertices)) {
        try {
          for (int v=0; v<3; ++v) {
            vdata[i][v] = boost::lexical_cast<double>(results[v+1]);
          }
        }
        catch (const boost::bad_lexical_cast &blc) {
          LOG(message_group::Warning,Location::None,"","Can't parse vertex line: %1$s",line);
          i = 10;
          continue;
        }
        if (++i == 3) {
          p->append_poly();
          p->append_vertex(vdata[0][0], vdata[0][1], vdata[0][2]);
          p->append_vertex(vdata[1][0], vdata[1][1], vdata[1][2]);
          p->append_vertex(vdata[2][0], vdata[2][1], vdata[2][2]);
        }
      }
    }
  }
  else if (binary && !f.eof() && f.good())
    {
      f.ignore(80-5+4);
      while (1) {
        stl_facet facet;
        read_stl_facet( f, facet );
        if (f.eof()) break;
        p->append_poly();
        p->append_vertex(facet.data.x1, facet.data.y1, facet.data.z1);
        p->append_vertex(facet.data.x2, facet.data.y2, facet.data.z2);
        p->append_vertex(facet.data.x3, facet.data.y3, facet.data.z3);
      }
    }
  return p;
}

int main(int argc, char *argv[])
{

  OpenSCAD::debug = "export_nef";
  CGAL_Nef_polyhedron *N = NULL;

  PolySet *ps = NULL;
  if (argc == 2) {
    std::string filename(argv[1]);
    std::string suffix = filename.extension().generic_string();
    boost::algorithm::to_lower(suffix);
    if (suffix == ".stl") {
      if (!(ps = import_stl(filename))) {
        std::cerr << "Error importing STL " << argv[1] << std::endl;
        exit(1);
      }
      std::cerr << "Imported " << ps->numFacets() << " polygons" << std::endl;
    }
    else if (suffix == ".nef3") {
      N = new CGAL_Nef_polyhedron(new CGAL_Nef_polyhedron3);
      std::ifstream stream(filename.c_str());
      stream >> *N->p3;
      std::cerr << "Imported Nef polyhedron" << std::endl;
    }
  }
  else {
    std::cerr << "Usage: " << argv[0] << " <file.stl>" << std::endl;
    exit(1);
  }

  if (ps && !N) N = createNefPolyhedronFromGeometry(*ps);

  export_stl(N, std::cout);
  std::cerr << "Done." << std::endl;
}
