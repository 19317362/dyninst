#ifndef PATCHAPI_CFGMAKER_H_
#define PATCHAPI_CFGMAKER_H_

#include "PatchCFG.h"

namespace Dyninst {

   namespace ParseAPI {
      class Function;
      class Block;
      class Edge;
   };

namespace PatchAPI {

   class PatchObject;
   class PatchFunction;
   class PatchBlock;
   class PatchEdge;


/* A factory class to make / copy CFG structures.
   We provide default implementations.  */

class CFGMaker {
  public:
    PATCHAPI_EXPORT CFGMaker() {}
    PATCHAPI_EXPORT virtual ~CFGMaker() {}

    // Make function
    PATCHAPI_EXPORT virtual PatchFunction* makeFunction(ParseAPI::Function*,
                                                        PatchObject*);
    PATCHAPI_EXPORT virtual PatchFunction* copyFunction(PatchFunction*,
                                                        PatchObject*);

    // Make block
    PATCHAPI_EXPORT virtual PatchBlock* makeBlock(ParseAPI::Block*,
                                                  PatchObject*);
    PATCHAPI_EXPORT virtual PatchBlock* copyBlock(PatchBlock*, PatchObject*);

    // Make edge
    PATCHAPI_EXPORT virtual PatchEdge* makeEdge(ParseAPI::Edge*, PatchBlock*,
                                PatchBlock*, PatchObject*);
    PATCHAPI_EXPORT virtual PatchEdge* copyEdge(PatchEdge*, PatchObject*);
};
typedef dyn_detail::boost::shared_ptr<CFGMaker> CFGMakerPtr;

}
}

#endif /* PATCHAPI_CFGMAKER_H_ */
