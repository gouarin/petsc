#ifndef included_ALE_Generator_hh
#define included_ALE_Generator_hh

#ifndef  included_ALE_Distribution_hh
#include <Distribution.hh>
#endif

#ifdef PETSC_HAVE_TRIANGLE
#include <triangle.h>
#endif
#ifdef PETSC_HAVE_TETGEN
#include <tetgen.h>
#endif

namespace ALE {
#ifdef PETSC_HAVE_TRIANGLE
  namespace Triangle {
    class Generator {
      typedef ALE::Mesh Mesh;
    public:
      static void initInput(struct triangulateio *inputCtx) {
        inputCtx->numberofpoints = 0;
        inputCtx->numberofpointattributes = 0;
        inputCtx->pointlist = NULL;
        inputCtx->pointattributelist = NULL;
        inputCtx->pointmarkerlist = NULL;
        inputCtx->numberofsegments = 0;
        inputCtx->segmentlist = NULL;
        inputCtx->segmentmarkerlist = NULL;
        inputCtx->numberoftriangleattributes = 0;
        inputCtx->trianglelist = NULL;
        inputCtx->numberofholes = 0;
        inputCtx->holelist = NULL;
        inputCtx->numberofregions = 0;
        inputCtx->regionlist = NULL;
      };
      static void initOutput(struct triangulateio *outputCtx) {
        outputCtx->pointlist = NULL;
        outputCtx->pointattributelist = NULL;
        outputCtx->pointmarkerlist = NULL;
        outputCtx->trianglelist = NULL;
        outputCtx->triangleattributelist = NULL;
        outputCtx->neighborlist = NULL;
        outputCtx->segmentlist = NULL;
        outputCtx->segmentmarkerlist = NULL;
        outputCtx->edgelist = NULL;
        outputCtx->edgemarkerlist = NULL;
      };
      static void finiOutput(struct triangulateio *outputCtx) {
        free(outputCtx->pointmarkerlist);
        free(outputCtx->edgelist);
        free(outputCtx->edgemarkerlist);
        free(outputCtx->trianglelist);
        free(outputCtx->neighborlist);
      };
      #undef __FUNCT__
      #define __FUNCT__ "generateMesh_Triangle"
      static Obj<Mesh> generateMesh(const Obj<Mesh>& boundary, const bool interpolate = false) {
        int                          dim   = 2;
        Obj<Mesh>                    mesh  = new Mesh(boundary->comm(), dim, boundary->debug());
        const Obj<Mesh::sieve_type>& sieve = boundary->getSieve();
        const bool                   createConvexHull = false;
        struct triangulateio in;
        struct triangulateio out;
        PetscErrorCode       ierr;

        initInput(&in);
        initOutput(&out);
        const Obj<Mesh::label_sequence>&    vertices    = boundary->depthStratum(0);
        const Obj<Mesh::label_type>&        markers     = boundary->getLabel("marker");
        const Obj<Mesh::real_section_type>& coordinates = boundary->getRealSection("coordinates");
        const Obj<Mesh::numbering_type>&    vNumbering  = boundary->getFactory()->getLocalNumbering(boundary, 0);

        in.numberofpoints = vertices->size();
        if (in.numberofpoints > 0) {
          ierr = PetscMalloc(in.numberofpoints * dim * sizeof(double), &in.pointlist);
          ierr = PetscMalloc(in.numberofpoints * sizeof(int), &in.pointmarkerlist);
          for(Mesh::label_sequence::iterator v_iter = vertices->begin(); v_iter != vertices->end(); ++v_iter) {
            const Mesh::real_section_type::value_type *array = coordinates->restrictPoint(*v_iter);
            const int                                  idx   = vNumbering->getIndex(*v_iter);

            for(int d = 0; d < dim; d++) {
              in.pointlist[idx*dim + d] = array[d];
            }
            in.pointmarkerlist[idx] = boundary->getValue(markers, *v_iter);
          }
        }
        const Obj<Mesh::label_sequence>& edges      = boundary->depthStratum(1);
        const Obj<Mesh::numbering_type>& eNumbering = boundary->getFactory()->getLocalNumbering(boundary, 1);

        in.numberofsegments = edges->size();
        if (in.numberofsegments > 0) {
          ierr = PetscMalloc(in.numberofsegments * 2 * sizeof(int), &in.segmentlist);
          ierr = PetscMalloc(in.numberofsegments * sizeof(int), &in.segmentmarkerlist);
          for(Mesh::label_sequence::iterator e_iter = edges->begin(); e_iter != edges->end(); ++e_iter) {
            const Obj<Mesh::sieve_type::traits::coneSequence>& cone = sieve->cone(*e_iter);
            const int                                          idx  = eNumbering->getIndex(*e_iter);
            int                                                v    = 0;
        
            for(Mesh::sieve_type::traits::coneSequence::iterator c_iter = cone->begin(); c_iter != cone->end(); ++c_iter) {
              in.segmentlist[idx*dim + (v++)] = vNumbering->getIndex(*c_iter);
            }
            in.segmentmarkerlist[idx] = boundary->getValue(markers, *e_iter);
          }
        }
        const Mesh::holes_type& holes = boundary->getHoles();

        in.numberofholes = holes.size();
        if (in.numberofholes > 0) {
          ierr = PetscMalloc(in.numberofholes*dim * sizeof(double), &in.holelist);
          for(int h = 0; h < in.numberofholes; ++h) {
            for(int d = 0; d < dim; ++d) {
              in.holelist[h*dim+d] = holes[h][d];
            }
          }
        }
        if (mesh->commRank() == 0) {
          std::string args("pqezQ");

          if (createConvexHull) {
            args += "c";
          }
          triangulate((char *) args.c_str(), &in, &out, NULL);
        }

        if (in.pointlist)         {ierr = PetscFree(in.pointlist);}
        if (in.pointmarkerlist)   {ierr = PetscFree(in.pointmarkerlist);}
        if (in.segmentlist)       {ierr = PetscFree(in.segmentlist);}
        if (in.segmentmarkerlist) {ierr = PetscFree(in.segmentmarkerlist);}
        if (in.holelist)          {ierr = PetscFree(in.holelist);}
        const Obj<Mesh::sieve_type> newSieve = new Mesh::sieve_type(mesh->comm(), mesh->debug());
        int     numCorners  = 3;
        int     numCells    = out.numberoftriangles;
        int    *cells       = out.trianglelist;
        int     numVertices = out.numberofpoints;
        double *coords      = out.pointlist;

        ALE::SieveBuilder<Mesh>::buildTopology(newSieve, dim, numCells, cells, numVertices, interpolate, numCorners, -1, mesh->getArrowSection("orientation"));
        mesh->setSieve(newSieve);
        mesh->stratify();
        ALE::SieveBuilder<Mesh>::buildCoordinates(mesh, dim, coords);
        const Obj<Mesh::label_type>& newMarkers = mesh->createLabel("marker");

        if (mesh->commRank() == 0) {
          for(int v = 0; v < out.numberofpoints; v++) {
            if (out.pointmarkerlist[v]) {
              mesh->setValue(newMarkers, v+out.numberoftriangles, out.pointmarkerlist[v]);
            }
          }
          if (interpolate) {
            for(int e = 0; e < out.numberofedges; e++) {
              if (out.edgemarkerlist[e]) {
                const Mesh::point_type vertexA(out.edgelist[e*2+0]+out.numberoftriangles);
                const Mesh::point_type vertexB(out.edgelist[e*2+1]+out.numberoftriangles);
                const Obj<Mesh::sieve_type::supportSet> edge = newSieve->nJoin(vertexA, vertexB, 1);

                mesh->setValue(newMarkers, *(edge->begin()), out.edgemarkerlist[e]);
              }
            }
          }
        }
        mesh->copyHoles(boundary);
        finiOutput(&out);
        return mesh;
      };
    };
    class Refiner {
    public:
      static Obj<Mesh> refineMesh(const Obj<Mesh>& serialMesh, const double maxVolumes[], const bool interpolate = false) {
        const int                    dim         = serialMesh->getDimension();
        const Obj<Mesh>              refMesh     = new Mesh(serialMesh->comm(), dim, serialMesh->debug());
        const Obj<Mesh::sieve_type>& serialSieve = serialMesh->getSieve();
        struct triangulateio in;
        struct triangulateio out;
        PetscErrorCode       ierr;

        Generator::initInput(&in);
        Generator::initOutput(&out);
        const Obj<Mesh::label_sequence>&    vertices    = serialMesh->depthStratum(0);
        const Obj<Mesh::label_type>&        markers     = serialMesh->getLabel("marker");
        const Obj<Mesh::real_section_type>& coordinates = serialMesh->getRealSection("coordinates");
        const Obj<Mesh::numbering_type>&    vNumbering  = serialMesh->getFactory()->getLocalNumbering(serialMesh, 0);

        in.numberofpoints = vertices->size();
        if (in.numberofpoints > 0) {
          ierr = PetscMalloc(in.numberofpoints * dim * sizeof(double), &in.pointlist);
          ierr = PetscMalloc(in.numberofpoints * sizeof(int), &in.pointmarkerlist);
          for(Mesh::label_sequence::iterator v_iter = vertices->begin(); v_iter != vertices->end(); ++v_iter) {
            const Mesh::real_section_type::value_type *array = coordinates->restrictPoint(*v_iter);
            const int                                  idx   = vNumbering->getIndex(*v_iter);

            for(int d = 0; d < dim; d++) {
              in.pointlist[idx*dim + d] = array[d];
            }
            in.pointmarkerlist[idx] = serialMesh->getValue(markers, *v_iter);
          }
        }
        const Obj<Mesh::label_sequence>& faces      = serialMesh->heightStratum(0);
        const Obj<Mesh::numbering_type>& fNumbering = serialMesh->getFactory()->getLocalNumbering(serialMesh, serialMesh->depth());

        in.numberofcorners   = 3;
        in.numberoftriangles = faces->size();
        in.trianglearealist  = (double *) maxVolumes;
        if (in.numberoftriangles > 0) {
          ierr = PetscMalloc(in.numberoftriangles*in.numberofcorners * sizeof(int), &in.trianglelist);
          if (serialMesh->depth() == 1) {
            for(Mesh::label_sequence::iterator f_iter = faces->begin(); f_iter != faces->end(); ++f_iter) {
              const Obj<Mesh::sieve_type::traits::coneSequence>& cone = serialSieve->cone(*f_iter);
              const int                                          idx  = fNumbering->getIndex(*f_iter);
              int                                                v    = 0;

              for(Mesh::sieve_type::traits::coneSequence::iterator c_iter = cone->begin(); c_iter != cone->end(); ++c_iter) {
                in.trianglelist[idx*in.numberofcorners + v++] = vNumbering->getIndex(*c_iter);
              }
            }
          } else if (serialMesh->depth() == 2) {
            for(Mesh::label_sequence::iterator f_iter = faces->begin(); f_iter != faces->end(); ++f_iter) {
              typedef ALE::SieveAlg<Mesh> sieve_alg_type;
              const Obj<sieve_alg_type::coneArray>& cone = sieve_alg_type::nCone(serialMesh, *f_iter, 2);
              const int                             idx  = fNumbering->getIndex(*f_iter);
              int                                   v    = 0;

              for(Mesh::sieve_type::coneArray::iterator c_iter = cone->begin(); c_iter != cone->end(); ++c_iter) {
                in.trianglelist[idx*in.numberofcorners + v++] = vNumbering->getIndex(*c_iter);
              }
            }
          } else {
            throw ALE::Exception("Invalid sieve: Cannot gives sieves of arbitrary depth to Triangle");
          }
        }
        if (serialMesh->depth() == 2) {
          const Obj<Mesh::label_sequence>&           edges    = serialMesh->depthStratum(1);
#define NEW_LABEL
#ifdef NEW_LABEL
          for(Mesh::label_sequence::iterator e_iter = edges->begin(); e_iter != edges->end(); ++e_iter) {
            if (serialMesh->getValue(markers, *e_iter)) {
              in.numberofsegments++;
            }
          }
          std::cout << "Number of segments: " << in.numberofsegments << std::endl;
          if (in.numberofsegments > 0) {
            int s = 0;

            ierr = PetscMalloc(in.numberofsegments * 2 * sizeof(int), &in.segmentlist);
            ierr = PetscMalloc(in.numberofsegments * sizeof(int), &in.segmentmarkerlist);
            for(Mesh::label_sequence::iterator e_iter = edges->begin(); e_iter != edges->end(); ++e_iter) {
              const int edgeMarker = serialMesh->getValue(markers, *e_iter);

              if (edgeMarker) {
                const Obj<Mesh::sieve_type::traits::coneSequence>& cone = serialSieve->cone(*e_iter);
                int                                                p    = 0;

                for(Mesh::sieve_type::traits::coneSequence::iterator v_iter = cone->begin(); v_iter != cone->end(); ++v_iter) {
                  in.segmentlist[s*2 + (p++)] = vNumbering->getIndex(*v_iter);
                }
                in.segmentmarkerlist[s++] = edgeMarker;
              }
            } 
          }
#else
          const Obj<Mesh::label_type::baseSequence>& boundary = markers->base();

          in.numberofsegments = 0;
          for(Mesh::label_type::baseSequence::iterator b_iter = boundary->begin(); b_iter != boundary->end(); ++b_iter) {
            for(Mesh::label_sequence::iterator e_iter = edges->begin(); e_iter != edges->end(); ++e_iter) {
              if (*b_iter == *e_iter) {
                in.numberofsegments++;
              }
            }
          }
          if (in.numberofsegments > 0) {
            int s = 0;

            ierr = PetscMalloc(in.numberofsegments * 2 * sizeof(int), &in.segmentlist);
            ierr = PetscMalloc(in.numberofsegments * sizeof(int), &in.segmentmarkerlist);
            for(Mesh::label_type::baseSequence::iterator b_iter = boundary->begin(); b_iter != boundary->end(); ++b_iter) {
              for(Mesh::label_sequence::iterator e_iter = edges->begin(); e_iter != edges->end(); ++e_iter) {
                if (*b_iter == *e_iter) {
                  const Obj<Mesh::sieve_type::traits::coneSequence>& cone = serialSieve->cone(*e_iter);
                  int                                                p    = 0;

                  for(Mesh::sieve_type::traits::coneSequence::iterator v_iter = cone->begin(); v_iter != cone->end(); ++v_iter) {
                    in.segmentlist[s*2 + (p++)] = vNumbering->getIndex(*v_iter);
                  }
                  in.segmentmarkerlist[s++] = serialMesh->getValue(markers, *e_iter);
                }
              }
            }
          }
#endif
        }

        in.numberofholes = 0;
        if (in.numberofholes > 0) {
          ierr = PetscMalloc(in.numberofholes * dim * sizeof(int), &in.holelist);
        }
        if (serialMesh->commRank() == 0) {
          std::string args("pqezQra");

          triangulate((char *) args.c_str(), &in, &out, NULL);
        }
        if (in.pointlist)         {ierr = PetscFree(in.pointlist);}
        if (in.pointmarkerlist)   {ierr = PetscFree(in.pointmarkerlist);}
        if (in.segmentlist)       {ierr = PetscFree(in.segmentlist);}
        if (in.segmentmarkerlist) {ierr = PetscFree(in.segmentmarkerlist);}
        if (in.trianglelist)      {ierr = PetscFree(in.trianglelist);}
        const Obj<Mesh::sieve_type> newSieve = new Mesh::sieve_type(serialMesh->comm(), serialMesh->debug());
        int     numCorners  = 3;
        int     numCells    = out.numberoftriangles;
        int    *cells       = out.trianglelist;
        int     numVertices = out.numberofpoints;
        double *coords      = out.pointlist;

        ALE::SieveBuilder<Mesh>::buildTopology(newSieve, dim, numCells, cells, numVertices, interpolate, numCorners, -1, refMesh->getArrowSection("orientation"));
        refMesh->setSieve(newSieve);
        refMesh->stratify();
        ALE::SieveBuilder<Mesh>::buildCoordinates(refMesh, dim, coords);
        const Obj<Mesh::label_type>& newMarkers = refMesh->createLabel("marker");

        if (refMesh->commRank() == 0) {
          for(int v = 0; v < out.numberofpoints; v++) {
            if (out.pointmarkerlist[v]) {
              refMesh->setValue(newMarkers, v+out.numberoftriangles, out.pointmarkerlist[v]);
            }
          }
          if (interpolate) {
            for(int e = 0; e < out.numberofedges; e++) {
              if (out.edgemarkerlist[e]) {
                const Mesh::point_type vertexA(out.edgelist[e*2+0]+out.numberoftriangles);
                const Mesh::point_type vertexB(out.edgelist[e*2+1]+out.numberoftriangles);
                const Obj<Mesh::sieve_type::supportSet> edge = newSieve->nJoin(vertexA, vertexB, 1);

                refMesh->setValue(newMarkers, *(edge->begin()), out.edgemarkerlist[e]);
              }
            }
          }
        }

        Generator::finiOutput(&out);
        if (refMesh->commSize() > 1) {
          return ALE::Distribution<Mesh>::distributeMesh(refMesh);
        }
        return refMesh;
      };
      static Obj<Mesh> refineMesh(const Obj<Mesh>& mesh, const Obj<Mesh::real_section_type>& maxVolumes, const bool interpolate = false) {
        Obj<Mesh>                          serialMesh       = ALE::Distribution<Mesh>::unifyMesh(mesh);
        const Obj<Mesh::real_section_type> serialMaxVolumes = ALE::Distribution<Mesh>::distributeSection(maxVolumes, serialMesh, serialMesh->getDistSendOverlap(), serialMesh->getDistRecvOverlap());

        return refineMesh(serialMesh, serialMaxVolumes->restrict(), interpolate);
      };
      static Obj<Mesh> refineMesh(const Obj<Mesh>& mesh, const double maxVolume, const bool interpolate = false) {
        Obj<Mesh> serialMesh;
        if (mesh->commSize() > 1) {
          serialMesh = ALE::Distribution<Mesh>::unifyMesh(mesh);
        } else {
          serialMesh = mesh;
        }
        const int numFaces         = serialMesh->heightStratum(0)->size();
        double   *serialMaxVolumes = new double[numFaces];

        for(int f = 0; f < numFaces; f++) {
          serialMaxVolumes[f] = maxVolume;
        }
        const Obj<Mesh> refMesh = refineMesh(serialMesh, serialMaxVolumes, interpolate);
        delete [] serialMaxVolumes;
        return refMesh;
      };
      static Obj<Mesh> refineMeshLocal(const Obj<Mesh>& mesh, const double maxVolumes[], const bool interpolate = false) {
        const int                    dim     = mesh->getDimension();
        const Obj<Mesh>              refMesh = new Mesh(mesh->comm(), dim, mesh->debug());
        const Obj<Mesh::sieve_type>& sieve   = mesh->getSieve();
        struct triangulateio in;
        struct triangulateio out;
        PetscErrorCode       ierr;

        Generator::initInput(&in);
        Generator::initOutput(&out);
        const Obj<Mesh::label_sequence>&    vertices    = mesh->depthStratum(0);
        const Obj<Mesh::label_type>&        markers     = mesh->getLabel("marker");
        const Obj<Mesh::real_section_type>& coordinates = mesh->getRealSection("coordinates");
        const Obj<Mesh::numbering_type>&    vNumbering  = mesh->getFactory()->getLocalNumbering(mesh, 0);

        in.numberofpoints = vertices->size();
        if (in.numberofpoints > 0) {
          ierr = PetscMalloc(in.numberofpoints * dim * sizeof(double), &in.pointlist);
          ierr = PetscMalloc(in.numberofpoints * sizeof(int), &in.pointmarkerlist);
          for(Mesh::label_sequence::iterator v_iter = vertices->begin(); v_iter != vertices->end(); ++v_iter) {
            const Mesh::real_section_type::value_type *array = coordinates->restrictPoint(*v_iter);
            const int                                  idx   = vNumbering->getIndex(*v_iter);

            for(int d = 0; d < dim; d++) {
              in.pointlist[idx*dim + d] = array[d];
            }
            in.pointmarkerlist[idx] = mesh->getValue(markers, *v_iter);
          }
        }
        const Obj<Mesh::label_sequence>& faces      = mesh->heightStratum(0);
        const Obj<Mesh::numbering_type>& fNumbering = mesh->getFactory()->getLocalNumbering(mesh, mesh->depth());

        in.numberofcorners   = 3;
        in.numberoftriangles = faces->size();
        in.trianglearealist  = (double *) maxVolumes;
        if (in.numberoftriangles > 0) {
          ierr = PetscMalloc(in.numberoftriangles*in.numberofcorners * sizeof(int), &in.trianglelist);
          if (mesh->depth() == 1) {
            for(Mesh::label_sequence::iterator f_iter = faces->begin(); f_iter != faces->end(); ++f_iter) {
              const Obj<Mesh::sieve_type::traits::coneSequence>& cone = sieve->cone(*f_iter);
              const int                                          idx  = fNumbering->getIndex(*f_iter);
              int                                                v    = 0;

              for(Mesh::sieve_type::traits::coneSequence::iterator c_iter = cone->begin(); c_iter != cone->end(); ++c_iter) {
                in.trianglelist[idx*in.numberofcorners + v++] = vNumbering->getIndex(*c_iter);
              }
            }
          } else if (mesh->depth() == 2) {
            for(Mesh::label_sequence::iterator f_iter = faces->begin(); f_iter != faces->end(); ++f_iter) {
              typedef ALE::SieveAlg<Mesh> sieve_alg_type;
              const Obj<sieve_alg_type::coneArray>& cone = sieve_alg_type::nCone(mesh, *f_iter, 2);
              const int                             idx  = fNumbering->getIndex(*f_iter);
              int                                   v    = 0;

              for(Mesh::sieve_type::coneArray::iterator c_iter = cone->begin(); c_iter != cone->end(); ++c_iter) {
                in.trianglelist[idx*in.numberofcorners + v++] = vNumbering->getIndex(*c_iter);
              }
            }
          } else {
            throw ALE::Exception("Invalid sieve: Cannot gives sieves of arbitrary depth to Triangle");
          }
        }
        if (mesh->depth() == 2) {
          const Obj<Mesh::label_sequence>& edges = mesh->depthStratum(1);
          for(Mesh::label_sequence::iterator e_iter = edges->begin(); e_iter != edges->end(); ++e_iter) {
            if (mesh->getValue(markers, *e_iter)) {
              in.numberofsegments++;
            }
          }
          std::cout << "Number of segments: " << in.numberofsegments << std::endl;
          if (in.numberofsegments > 0) {
            int s = 0;

            ierr = PetscMalloc(in.numberofsegments * 2 * sizeof(int), &in.segmentlist);
            ierr = PetscMalloc(in.numberofsegments * sizeof(int), &in.segmentmarkerlist);
            for(Mesh::label_sequence::iterator e_iter = edges->begin(); e_iter != edges->end(); ++e_iter) {
              const int edgeMarker = mesh->getValue(markers, *e_iter);

              if (edgeMarker) {
                const Obj<Mesh::sieve_type::traits::coneSequence>& cone = sieve->cone(*e_iter);
                int                                                p    = 0;

                for(Mesh::sieve_type::traits::coneSequence::iterator v_iter = cone->begin(); v_iter != cone->end(); ++v_iter) {
                  in.segmentlist[s*2 + (p++)] = vNumbering->getIndex(*v_iter);
                }
                in.segmentmarkerlist[s++] = edgeMarker;
              }
            } 
          }
        }

        in.numberofholes = 0;
        if (in.numberofholes > 0) {
          ierr = PetscMalloc(in.numberofholes * dim * sizeof(int), &in.holelist);
        }
	std::string args("pqezQra");

	triangulate((char *) args.c_str(), &in, &out, NULL);
        if (in.pointlist)         {ierr = PetscFree(in.pointlist);}
        if (in.pointmarkerlist)   {ierr = PetscFree(in.pointmarkerlist);}
        if (in.segmentlist)       {ierr = PetscFree(in.segmentlist);}
        if (in.segmentmarkerlist) {ierr = PetscFree(in.segmentmarkerlist);}
        if (in.trianglelist)      {ierr = PetscFree(in.trianglelist);}
        const Obj<Mesh::sieve_type> newSieve = new Mesh::sieve_type(mesh->comm(), mesh->debug());
        int     numCorners  = 3;
        int     numCells    = out.numberoftriangles;
        int    *cells       = out.trianglelist;
        int     numVertices = out.numberofpoints;
        double *coords      = out.pointlist;

        ALE::SieveBuilder<Mesh>::buildTopologyMultiple(newSieve, dim, numCells, cells, numVertices, interpolate, numCorners, -1, refMesh->getArrowSection("orientation"));
        refMesh->setSieve(newSieve);
        refMesh->stratify();
        ALE::SieveBuilder<Mesh>::buildCoordinatesMultiple(refMesh, dim, coords);
        const Obj<Mesh::label_type>& newMarkers = refMesh->createLabel("marker");

	for(int v = 0; v < out.numberofpoints; v++) {
	  if (out.pointmarkerlist[v]) {
	    refMesh->setValue(newMarkers, v+out.numberoftriangles, out.pointmarkerlist[v]);
	  }
	}
	if (interpolate) {
	  for(int e = 0; e < out.numberofedges; e++) {
	    if (out.edgemarkerlist[e]) {
	      const Mesh::point_type vertexA(out.edgelist[e*2+0]+out.numberoftriangles);
	      const Mesh::point_type vertexB(out.edgelist[e*2+1]+out.numberoftriangles);
	      const Obj<Mesh::sieve_type::supportSet> edge = newSieve->nJoin(vertexA, vertexB, 1);

	      refMesh->setValue(newMarkers, *(edge->begin()), out.edgemarkerlist[e]);
	    }
	  }
	}

        Generator::finiOutput(&out);
        return refMesh;
      };
      static Obj<Mesh> refineMeshLocal(const Obj<Mesh>& mesh, const double maxVolume, const bool interpolate = false) {
        const int numLocalFaces   = mesh->heightStratum(0)->size();
        double   *localMaxVolumes = new double[numLocalFaces];

        for(int f = 0; f < numLocalFaces; f++) {
          localMaxVolumes[f] = maxVolume;
        }
        const Obj<Mesh> refMesh = refineMeshLocal(mesh, localMaxVolumes, interpolate);
	const Obj<Mesh::sieve_type> refSieve = refMesh->getSieve();
        delete [] localMaxVolumes;
#if 0
	typedef typename ALE::New::Completion<Mesh, typename Mesh::sieve_type::point_type> sieveCompletion;
	// This is where we enforce consistency over the overlap
	//   We need somehow to update the overlap to account for the new stuff
	//   
	//   1) Since we are refining only, the vertices are invariant
	//   2) We need to make another label for the interprocess boundaries so
	//      that Triangle will respect them
	//   3) We then throw all that label into the new overlap
	//
	// Alternative: Figure out explicitly which segments were refined, and then
	//   communicated the refinement over the old overlap. Use this info to locally
	//   construct the new overlap and flip to get a decent mesh
	sieveCompletion::scatterCones(refSieve, refSieve, reMesh->getDistSendOverlap(), refMesh->getDistRecvOverlap(), refMesh);
#endif
        return refMesh;
      };
    };
  };
#endif
#ifdef PETSC_HAVE_TETGEN
  namespace TetGen {
    class Generator {
    public:
      static Obj<Mesh> generateMesh(const Obj<Mesh>& boundary, const bool interpolate = false) {
        typedef ALE::SieveAlg<Mesh> sieve_alg_type;
        const int         dim   = 3;
        Obj<Mesh>         mesh  = new Mesh(boundary->comm(), dim, boundary->debug());
        const PetscMPIInt rank  = mesh->commRank();
        const bool        createConvexHull = false;
        ::tetgenio        in;
        ::tetgenio        out;

        const Obj<Mesh::label_sequence>&    vertices    = boundary->depthStratum(0);
        const Obj<Mesh::numbering_type>&    vNumbering  = boundary->getFactory()->getLocalNumbering(boundary, 0);
        const Obj<Mesh::real_section_type>& coordinates = boundary->getRealSection("coordinates");
        const Obj<Mesh::label_type>&        markers     = boundary->getLabel("marker");


        in.numberofpoints = vertices->size();
        if (in.numberofpoints > 0) {
          in.pointlist       = new double[in.numberofpoints*dim];
          in.pointmarkerlist = new int[in.numberofpoints];
          for(Mesh::label_sequence::iterator v_iter = vertices->begin(); v_iter != vertices->end(); ++v_iter) {
            const Mesh::real_section_type::value_type *array = coordinates->restrictPoint(*v_iter);
            const int                                  idx   = vNumbering->getIndex(*v_iter);

            for(int d = 0; d < dim; d++) {
              in.pointlist[idx*dim + d] = array[d];
            }
            in.pointmarkerlist[idx] = boundary->getValue(markers, *v_iter);
          }
        }

        const Obj<Mesh::label_sequence>& facets     = boundary->depthStratum(boundary->depth());
        const Obj<Mesh::numbering_type>& fNumbering = boundary->getFactory()->getLocalNumbering(boundary, boundary->depth());

        in.numberoffacets = facets->size();
        if (in.numberoffacets > 0) {
          in.facetlist       = new tetgenio::facet[in.numberoffacets];
          in.facetmarkerlist = new int[in.numberoffacets];
          for(Mesh::label_sequence::iterator f_iter = facets->begin(); f_iter != facets->end(); ++f_iter) {
            const Obj<sieve_alg_type::coneArray>& cone = sieve_alg_type::nCone(boundary, *f_iter, boundary->depth());
            const int                             idx  = fNumbering->getIndex(*f_iter);

            in.facetlist[idx].numberofpolygons = 1;
            in.facetlist[idx].polygonlist      = new tetgenio::polygon[in.facetlist[idx].numberofpolygons];
            in.facetlist[idx].numberofholes    = 0;
            in.facetlist[idx].holelist         = NULL;

            tetgenio::polygon *poly = in.facetlist[idx].polygonlist;
            int                c    = 0;

            poly->numberofvertices = cone->size();
            poly->vertexlist       = new int[poly->numberofvertices];
            for(sieve_alg_type::coneArray::iterator c_iter = cone->begin(); c_iter != cone->end(); ++c_iter) {
              const int vIdx = vNumbering->getIndex(*c_iter);

              poly->vertexlist[c++] = vIdx;
            }
            in.facetmarkerlist[idx] = boundary->getValue(markers, *f_iter);
          }
        }

        in.numberofholes = 0;
        if (rank == 0) {
          // Normal operation
          std::string args("pqezQ");
          // Just make tetrahedrons
//           std::string args("efzV");
          // Adds a center point
//           std::string args("pqezQi");
//           in.numberofaddpoints = 1;
//           in.addpointlist      = new double[in.numberofaddpoints*dim];
//           in.addpointlist[0]   = 0.5;
//           in.addpointlist[1]   = 0.5;
//           in.addpointlist[2]   = 0.5;

          if (createConvexHull) args += "c";
          ::tetrahedralize((char *) args.c_str(), &in, &out);
        }
        const Obj<Mesh::sieve_type> newSieve = new Mesh::sieve_type(mesh->comm(), mesh->debug());
        int     numCorners  = 4;
        int     numCells    = out.numberoftetrahedra;
        int    *cells       = out.tetrahedronlist;
        int     numVertices = out.numberofpoints;
        double *coords      = out.pointlist;

        if (!interpolate) {
          for(int c = 0; c < numCells; ++c) {
            int tmp = cells[c*4+0];
            cells[c*4+0] = cells[c*4+1];
            cells[c*4+1] = tmp;
          }
        }
        ALE::SieveBuilder<Mesh>::buildTopology(newSieve, dim, numCells, cells, numVertices, interpolate, numCorners, -1, mesh->getArrowSection("orientation"));
        mesh->setSieve(newSieve);
        mesh->stratify();
        ALE::SieveBuilder<Mesh>::buildCoordinates(mesh, dim, coords);
        const Obj<Mesh::label_type>& newMarkers = mesh->createLabel("marker");
  
        for(int v = 0; v < out.numberofpoints; v++) {
          if (out.pointmarkerlist[v]) {
            mesh->setValue(newMarkers, v+out.numberoftetrahedra, out.pointmarkerlist[v]);
          }
        }
        if (interpolate) {
          if (out.edgemarkerlist) {
            for(int e = 0; e < out.numberofedges; e++) {
              if (out.edgemarkerlist[e]) {
                Mesh::point_type endpointA(out.edgelist[e*2+0]+out.numberoftetrahedra);
                Mesh::point_type endpointB(out.edgelist[e*2+1]+out.numberoftetrahedra);
                Obj<Mesh::sieve_type::supportSet> edge = newSieve->nJoin(endpointA, endpointB, 1);

                mesh->setValue(newMarkers, *edge->begin(), out.edgemarkerlist[e]);
              }
            }
          }
          if (out.trifacemarkerlist) {
            // Work around TetGen bug for raw tetrahedralization
            //   The boundary faces are 0,1,4,5,8,9,11,12,13,15,16,17
//             for(int f = 0; f < out.numberoftrifaces; f++) {
//               if (out.trifacemarkerlist[f]) {
//                 out.trifacemarkerlist[f] = 0;
//               } else {
//                 out.trifacemarkerlist[f] = 1;
//               }
//             }
            for(int f = 0; f < out.numberoftrifaces; f++) {
              if (out.trifacemarkerlist[f]) {
                Mesh::point_type cornerA(out.trifacelist[f*3+0]+out.numberoftetrahedra);
                Mesh::point_type cornerB(out.trifacelist[f*3+1]+out.numberoftetrahedra);
                Mesh::point_type cornerC(out.trifacelist[f*3+2]+out.numberoftetrahedra);
                Obj<Mesh::sieve_type::supportSet> corners = Mesh::sieve_type::supportSet();
                Obj<Mesh::sieve_type::supportSet> edges   = Mesh::sieve_type::supportSet();
                corners->clear();corners->insert(cornerA);corners->insert(cornerB);
                edges->insert(*newSieve->nJoin1(corners)->begin());
                corners->clear();corners->insert(cornerB);corners->insert(cornerC);
                edges->insert(*newSieve->nJoin1(corners)->begin());
                corners->clear();corners->insert(cornerC);corners->insert(cornerA);
                edges->insert(*newSieve->nJoin1(corners)->begin());
                const Mesh::point_type          face       = *newSieve->nJoin1(edges)->begin();
                const int                       faceMarker = out.trifacemarkerlist[f];
                const Obj<Mesh::coneArray>      closure    = sieve_alg_type::closure(mesh, face);
                const Mesh::coneArray::iterator end        = closure->end();

                for(Mesh::coneArray::iterator cl_iter = closure->begin(); cl_iter != end; ++cl_iter) {
                  mesh->setValue(newMarkers, *cl_iter, faceMarker);
                }
              }
            }
          }
        }
        return mesh;
      };
    };
    class Refiner {
    public:
      static Obj<Mesh> refineMesh(const Obj<Mesh>& serialMesh, const double maxVolumes[], const bool interpolate = false) {
        typedef ALE::SieveAlg<Mesh> sieve_alg_type;
        const int       dim     = serialMesh->getDimension();
        const int       depth   = serialMesh->depth();
        const Obj<Mesh> refMesh = new Mesh(serialMesh->comm(), dim, serialMesh->debug());
        ::tetgenio      in;
        ::tetgenio      out;

        const Obj<Mesh::label_sequence>&    vertices    = serialMesh->depthStratum(0);
        const Obj<Mesh::label_type>&        markers     = serialMesh->getLabel("marker");
        const Obj<Mesh::real_section_type>& coordinates = serialMesh->getRealSection("coordinates");
        const Obj<Mesh::numbering_type>&    vNumbering  = serialMesh->getFactory()->getLocalNumbering(serialMesh, 0);

        in.numberofpoints = vertices->size();
        if (in.numberofpoints > 0) {
          in.pointlist       = new double[in.numberofpoints*dim];
          in.pointmarkerlist = new int[in.numberofpoints];
          for(Mesh::label_sequence::iterator v_iter = vertices->begin(); v_iter != vertices->end(); ++v_iter) {
            const Mesh::real_section_type::value_type *array = coordinates->restrictPoint(*v_iter);
            const int                                  idx   = vNumbering->getIndex(*v_iter);

            for(int d = 0; d < dim; d++) {
              in.pointlist[idx*dim + d] = array[d];
            }
            in.pointmarkerlist[idx] = serialMesh->getValue(markers, *v_iter);
          }
        }
        const Obj<Mesh::label_sequence>& cells      = serialMesh->heightStratum(0);
        const Obj<Mesh::numbering_type>& cNumbering = serialMesh->getFactory()->getLocalNumbering(serialMesh, depth);

        in.numberofcorners       = 4;
        in.numberoftetrahedra    = cells->size();
        in.tetrahedronvolumelist = (double *) maxVolumes;
        if (in.numberoftetrahedra > 0) {
          in.tetrahedronlist     = new int[in.numberoftetrahedra*in.numberofcorners];
          for(Mesh::label_sequence::iterator c_iter = cells->begin(); c_iter != cells->end(); ++c_iter) {
            typedef ALE::SieveAlg<Mesh> sieve_alg_type;
            const Obj<sieve_alg_type::coneArray>& cone = sieve_alg_type::nCone(serialMesh, *c_iter, depth);
            const int                             idx  = cNumbering->getIndex(*c_iter);
            int                                   v    = 0;

            for(Mesh::sieve_type::coneArray::iterator v_iter = cone->begin(); v_iter != cone->end(); ++v_iter) {
              in.tetrahedronlist[idx*in.numberofcorners + v++] = vNumbering->getIndex(*v_iter);
            }
          }
        }
        if (serialMesh->depth() == 3) {
          const Obj<Mesh::label_sequence>& boundary = serialMesh->getLabelStratum("marker", 1);

          in.numberoftrifaces = 0;
          for(Mesh::label_sequence::iterator b_iter = boundary->begin(); b_iter != boundary->end(); ++b_iter) {
            if (serialMesh->height(*b_iter) == 1) {
              in.numberoftrifaces++;
            }
          }
          if (in.numberoftrifaces > 0) {
            int f = 0;

            in.trifacelist       = new int[in.numberoftrifaces*3];
            in.trifacemarkerlist = new int[in.numberoftrifaces];
            for(Mesh::label_sequence::iterator b_iter = boundary->begin(); b_iter != boundary->end(); ++b_iter) {
              if (serialMesh->height(*b_iter) == 1) {
                const Obj<Mesh::coneArray>& cone = sieve_alg_type::nCone(serialMesh, *b_iter, 2);
                int                         p    = 0;

                for(Mesh::coneArray::iterator v_iter = cone->begin(); v_iter != cone->end(); ++v_iter) {
                  in.trifacelist[f*3 + (p++)] = vNumbering->getIndex(*v_iter);
                }
                in.trifacemarkerlist[f++] = serialMesh->getValue(markers, *b_iter);
              }
            }
          }
        }

        in.numberofholes = 0;
        if (serialMesh->commRank() == 0) {
          std::string args("qezQra");

          ::tetrahedralize((char *) args.c_str(), &in, &out);
        }
        in.tetrahedronvolumelist = NULL;
        const Obj<Mesh::sieve_type> newSieve = new Mesh::sieve_type(refMesh->comm(), refMesh->debug());
        int     numCorners  = 4;
        int     numCells    = out.numberoftetrahedra;
        int    *newCells       = out.tetrahedronlist;
        int     numVertices = out.numberofpoints;
        double *coords      = out.pointlist;

        if (!interpolate) {
          for(int c = 0; c < numCells; ++c) {
            int tmp = newCells[c*4+0];
            newCells[c*4+0] = newCells[c*4+1];
            newCells[c*4+1] = tmp;
          }
        }
        ALE::SieveBuilder<Mesh>::buildTopology(newSieve, dim, numCells, newCells, numVertices, interpolate, numCorners, -1, refMesh->getArrowSection("orientation"));
        refMesh->setSieve(newSieve);
        refMesh->stratify();
        ALE::SieveBuilder<Mesh>::buildCoordinates(refMesh, dim, coords);
        const Obj<Mesh::label_type>& newMarkers = refMesh->createLabel("marker");


        for(int v = 0; v < out.numberofpoints; v++) {
          if (out.pointmarkerlist[v]) {
            refMesh->setValue(newMarkers, v+out.numberoftetrahedra, out.pointmarkerlist[v]);
          }
        }
        if (interpolate) {
          if (out.edgemarkerlist) {
            for(int e = 0; e < out.numberofedges; e++) {
              if (out.edgemarkerlist[e]) {
                Mesh::point_type endpointA(out.edgelist[e*2+0]+out.numberoftetrahedra);
                Mesh::point_type endpointB(out.edgelist[e*2+1]+out.numberoftetrahedra);
                Obj<Mesh::sieve_type::supportSet> edge = newSieve->nJoin(endpointA, endpointB, 1);

                refMesh->setValue(newMarkers, *edge->begin(), out.edgemarkerlist[e]);
              }
            }
          }
          if (out.trifacemarkerlist) {
            for(int f = 0; f < out.numberoftrifaces; f++) {
              if (out.trifacemarkerlist[f]) {
                Mesh::point_type cornerA(out.trifacelist[f*3+0]+out.numberoftetrahedra);
                Mesh::point_type cornerB(out.trifacelist[f*3+1]+out.numberoftetrahedra);
                Mesh::point_type cornerC(out.trifacelist[f*3+2]+out.numberoftetrahedra);
                Obj<Mesh::sieve_type::supportSet> corners = Mesh::sieve_type::supportSet();
                Obj<Mesh::sieve_type::supportSet> edges   = Mesh::sieve_type::supportSet();
                corners->clear();corners->insert(cornerA);corners->insert(cornerB);
                edges->insert(*newSieve->nJoin1(corners)->begin());
                corners->clear();corners->insert(cornerB);corners->insert(cornerC);
                edges->insert(*newSieve->nJoin1(corners)->begin());
                corners->clear();corners->insert(cornerC);corners->insert(cornerA);
                edges->insert(*newSieve->nJoin1(corners)->begin());
                const Mesh::point_type          face       = *newSieve->nJoin1(edges)->begin();
                const int                       faceMarker = out.trifacemarkerlist[f];
                const Obj<Mesh::coneArray>      closure    = sieve_alg_type::closure(refMesh, face);
                const Mesh::coneArray::iterator end        = closure->end();

                for(Mesh::coneArray::iterator cl_iter = closure->begin(); cl_iter != end; ++cl_iter) {
                  refMesh->setValue(newMarkers, *cl_iter, faceMarker);
                }
              }
            }
          }
        }
        if (refMesh->commSize() > 1) {
          return ALE::Distribution<Mesh>::distributeMesh(refMesh);
        }
        return refMesh;
      };
      static Obj<Mesh> refineMesh(const Obj<Mesh>& mesh, const Obj<Mesh::real_section_type>& maxVolumes, const bool interpolate = false) {
        Obj<Mesh>                          serialMesh       = ALE::Distribution<Mesh>::unifyMesh(mesh);
        const Obj<Mesh::real_section_type> serialMaxVolumes = ALE::Distribution<Mesh>::distributeSection(maxVolumes, serialMesh, serialMesh->getDistSendOverlap(), serialMesh->getDistRecvOverlap());

        return refineMesh(serialMesh, serialMaxVolumes->restrict(), interpolate);
      };
      static Obj<Mesh> refineMesh(const Obj<Mesh>& mesh, const double maxVolume, const bool interpolate = false) {
        Obj<Mesh> serialMesh;
        if (mesh->commSize() > 1) {
          serialMesh = ALE::Distribution<Mesh>::unifyMesh(mesh);
        } else {
          serialMesh = mesh;
        }
        const int numCells         = serialMesh->heightStratum(0)->size();
        double   *serialMaxVolumes = new double[numCells];

        for(int c = 0; c < numCells; c++) {
          serialMaxVolumes[c] = maxVolume;
        }
        const Obj<Mesh> refMesh = refineMesh(serialMesh, serialMaxVolumes, interpolate);
        delete [] serialMaxVolumes;
        return refMesh;
      };
    };
  };
#endif
  class Generator {
    typedef ALE::Mesh Mesh;
  public:
    static Obj<Mesh> generateMesh(const Obj<Mesh>& boundary, const bool interpolate = false) {
      int dim = boundary->getDimension();

      if (dim == 1) {
#ifdef PETSC_HAVE_TRIANGLE
        return ALE::Triangle::Generator::generateMesh(boundary, interpolate);
#else
        throw ALE::Exception("Mesh generation currently requires Triangle to be installed. Use --download-triangle during configure.");
#endif
      } else if (dim == 2) {
#ifdef PETSC_HAVE_TETGEN
        return ALE::TetGen::Generator::generateMesh(boundary, interpolate);
#else
        throw ALE::Exception("Mesh generation currently requires TetGen to be installed. Use --download-tetgen during configure.");
#endif
      }
      return NULL;
    };
    static Obj<Mesh> refineMesh(const Obj<Mesh>& mesh, const Obj<Mesh::real_section_type>& maxVolumes, const bool interpolate = false) {
      int dim = mesh->getDimension();

      if (dim == 2) {
#ifdef PETSC_HAVE_TRIANGLE
        return ALE::Triangle::Refiner::refineMesh(mesh, maxVolumes, interpolate);
#else
        throw ALE::Exception("Mesh refinement currently requires Triangle to be installed. Use --download-triangle during configure.");
#endif
      } else if (dim == 3) {
#ifdef PETSC_HAVE_TETGEN
        return ALE::TetGen::Refiner::refineMesh(mesh, maxVolumes, interpolate);
#else
        throw ALE::Exception("Mesh refinement currently requires TetGen to be installed. Use --download-tetgen during configure.");
#endif
      }
      return NULL;
    };
    static Obj<Mesh> refineMesh(const Obj<Mesh>& mesh, const double maxVolume, const bool interpolate = false) {
      int dim = mesh->getDimension();

      if (dim == 2) {
#ifdef PETSC_HAVE_TRIANGLE
        return ALE::Triangle::Refiner::refineMesh(mesh, maxVolume, interpolate);
#else
        throw ALE::Exception("Mesh refinement currently requires Triangle to be installed. Use --download-triangle during configure.");
#endif
      } else if (dim == 3) {
#ifdef PETSC_HAVE_TETGEN
        return ALE::TetGen::Refiner::refineMesh(mesh, maxVolume, interpolate);
#else
        throw ALE::Exception("Mesh refinement currently requires TetGen to be installed. Use --download-tetgen during configure.");
#endif
      }
      return NULL;
    };
    static Obj<Mesh> refineMeshLocal(const Obj<Mesh>& mesh, const double maxVolume, const bool interpolate = false) {
      int dim = mesh->getDimension();

      if (dim == 2) {
#ifdef PETSC_HAVE_TRIANGLE
        return ALE::Triangle::Refiner::refineMeshLocal(mesh, maxVolume, interpolate);
#else
        throw ALE::Exception("Mesh refinement currently requires Triangle to be installed. Use --download-triangle during configure.");
#endif
      } else if (dim == 3) {
#ifdef PETSC_HAVE_TETGEN
        return ALE::TetGen::Refiner::refineMesh(mesh, maxVolume, interpolate);
#else
        throw ALE::Exception("Mesh refinement currently requires TetGen to be installed. Use --download-tetgen during configure.");
#endif
      }
      return NULL;
    };
  };
}

#endif
