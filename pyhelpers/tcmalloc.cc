
/* AUTHOR Kashirin Alex (kashirin.alex@gmail.com) */

#include <gperftools/malloc_extension.h>

#include <pybind11/pybind11.h>
namespace py = pybind11;



PYBIND11_MODULE(tcmalloc, m) {
  m.doc() = "Python Helper to TCMalloc API";
  
  m.def("ReleaseFreeMemory", []() {
      MallocExtension::instance()->ReleaseFreeMemory();
    })
  ;
}