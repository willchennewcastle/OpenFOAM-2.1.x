#include "checkTopology.H"
#include "polyMesh.H"
#include "Time.H"
#include "regionSplit.H"
#include "cellSet.H"
#include "faceSet.H"
#include "pointSet.H"
#include "IOmanip.H"
#include "emptyPolyPatch.H"

Foam::label Foam::checkTopology
(
    const polyMesh& mesh,
    const bool allTopology,
    const bool allGeometry
)
{
    label noFailedChecks = 0;

    Info<< "Checking topology..." << endl;

    // Check if the boundary definition is unique
    mesh.boundaryMesh().checkDefinition(true);

    // Check that empty patches cover all sides of the mesh
    {
        label nEmpty = 0;
        forAll(mesh.boundaryMesh(), patchI)
        {
            if (isA<emptyPolyPatch>(mesh.boundaryMesh()[patchI]))
            {
                nEmpty += mesh.boundaryMesh()[patchI].size();
            }
        }
        reduce(nEmpty, sumOp<label>());
        label nTotCells = returnReduce(mesh.cells().size(), sumOp<label>());

        // These are actually warnings, not errors.
        if (nTotCells && (nEmpty % nTotCells))
        {
            Info<< " ***Total number of faces on empty patches"
                << " is not divisible by the number of cells in the mesh."
                << " Hence this mesh is not 1D or 2D."
                << endl;
        }
    }

    // Check if the boundary processor patches are correct
    mesh.boundaryMesh().checkParallelSync(true);

    // Check names of zones are equal
    mesh.cellZones().checkDefinition(true);
    if (mesh.cellZones().checkParallelSync(true))
    {
        noFailedChecks++;
    }
    mesh.faceZones().checkDefinition(true);
    if (mesh.faceZones().checkParallelSync(true))
    {
        noFailedChecks++;
    }
    mesh.pointZones().checkDefinition(true);
    if (mesh.pointZones().checkParallelSync(true))
    {
        noFailedChecks++;
    }


    {
        cellSet cells(mesh, "illegalCells", mesh.nCells()/100);

        forAll(mesh.cells(), cellI)
        {
            const cell& cFaces = mesh.cells()[cellI];

            if (cFaces.size() <= 3)
            {
                cells.insert(cellI);
            }
            forAll(cFaces, i)
            {
                if (cFaces[i] < 0 || cFaces[i] >= mesh.nFaces())
                {
                    cells.insert(cellI);
                    break;  
                }
            }
        }
        label nCells = returnReduce(cells.size(), sumOp<label>());

        if (nCells > 0)
        {
            Info<< "    Illegal cells (less than 4 faces or out of range faces)"
                << " found,  number of cells: " << nCells << endl;
            noFailedChecks++;

            Info<< "  <<Writing " << nCells
                << " illegal cells to set " << cells.name() << endl;
            cells.instance() = mesh.pointsInstance();
            cells.write();
        }
        else
        {
            Info<< "    Cell to face addressing OK." << endl;
        }
    }


    {
        pointSet points(mesh, "unusedPoints", mesh.nPoints()/100);
        if (mesh.checkPoints(true, &points))
        {
            noFailedChecks++;

            label nPoints = returnReduce(points.size(), sumOp<label>());

            Info<< "  <<Writing " << nPoints
                << " unused points to set " << points.name() << endl;
            points.instance() = mesh.pointsInstance();
            points.write();
        }
    }

    {
        faceSet faces(mesh, "upperTriangularFace", mesh.nFaces()/100);
        if (mesh.checkUpperTriangular(true, &faces))
        {
            noFailedChecks++;
        }

        label nFaces = returnReduce(faces.size(), sumOp<label>());

        if (nFaces > 0)
        {
            Info<< "  <<Writing " << nFaces
                << " unordered faces to set " << faces.name() << endl;
            faces.instance() = mesh.pointsInstance();
            faces.write();
        }
    }

    {
        faceSet faces(mesh, "outOfRangeFaces", mesh.nFaces()/100);
        if (mesh.checkFaceVertices(true, &faces))
        {
            noFailedChecks++;

            label nFaces = returnReduce(faces.size(), sumOp<label>());

            Info<< "  <<Writing " << nFaces
                << " faces with out-of-range or duplicate vertices to set "
                << faces.name() << endl;
            faces.instance() = mesh.pointsInstance();
            faces.write();
        }
    }

    if (allTopology)
    {
        cellSet cells(mesh, "zipUpCells", mesh.nCells()/100);
        if (mesh.checkCellsZipUp(true, &cells))
        {
            noFailedChecks++;

            label nCells = returnReduce(cells.size(), sumOp<label>());

            Info<< "  <<Writing " << nCells
                << " cells with over used edges to set " << cells.name()
                << endl;
            cells.instance() = mesh.pointsInstance();
            cells.write();
        }
    }

    if (allTopology)
    {
        faceSet faces(mesh, "edgeFaces", mesh.nFaces()/100);
        if (mesh.checkFaceFaces(true, &faces))
        {
            noFailedChecks++;
        }

        label nFaces = returnReduce(faces.size(), sumOp<label>());
        if (nFaces > 0)
        {
            Info<< "  <<Writing " << nFaces
                << " faces with non-standard edge connectivity to set "
                << faces.name() << endl;
            faces.instance() = mesh.pointsInstance();
            faces.write();
        }
    }

    if (allTopology)
    {
        labelList nInternalFaces(mesh.nCells(), 0);

        for (label faceI = 0; faceI < mesh.nInternalFaces(); faceI++)
        {
            nInternalFaces[mesh.faceOwner()[faceI]]++;
            nInternalFaces[mesh.faceNeighbour()[faceI]]++;
        }
        const polyBoundaryMesh& patches = mesh.boundaryMesh();
        forAll(patches, patchI)
        {
            if (patches[patchI].coupled())
            {
                const labelUList& owners = patches[patchI].faceCells();

                forAll(owners, i)
                {
                    nInternalFaces[owners[i]]++;
                }
            }
        }

        cellSet oneCells(mesh, "oneInternalFaceCells", mesh.nCells()/100);
        cellSet twoCells(mesh, "twoInternalFacesCells", mesh.nCells()/100);

        forAll(nInternalFaces, cellI)
        {
            if (nInternalFaces[cellI] <= 1)
            {
                oneCells.insert(cellI);
            }
            else if (nInternalFaces[cellI] == 2)
            {
                twoCells.insert(cellI);
            }
        }

        label nOneCells = returnReduce(oneCells.size(), sumOp<label>());

        if (nOneCells > 0)
        {
            Info<< "  <<Writing " << nOneCells
                << " cells with with zero or one non-boundary face to set "
                << oneCells.name()
                << endl;
            oneCells.instance() = mesh.pointsInstance();
            oneCells.write();
        }

        label nTwoCells = returnReduce(twoCells.size(), sumOp<label>());

        if (nTwoCells > 0)
        {
            Info<< "  <<Writing " << nTwoCells
                << " cells with with two non-boundary faces to set "
                << twoCells.name()
                << endl;
            twoCells.instance() = mesh.pointsInstance();
            twoCells.write();
        }
    }

    {
        regionSplit rs(mesh);

        if (rs.nRegions() <= 1)
        {
            Info<< "    Number of regions: " << rs.nRegions() << " (OK)."
                << endl;

        }
        else
        {
            Info<< "   *Number of regions: " << rs.nRegions() << endl;

            Info<< "    The mesh has multiple regions which are not connected "
                   "by any face." << endl
                << "  <<Writing region information to "
                << mesh.time().timeName()/"cellToRegion"
                << endl;

            labelIOList ctr
            (
                IOobject
                (
                    "cellToRegion",
                    mesh.time().timeName(),
                    mesh,
                    IOobject::NO_READ,
                    IOobject::NO_WRITE
                ),
                rs
            );
            ctr.write();
        }
    }

    if (!Pstream::parRun())
    {
        Pout<< "\nChecking patch topology for multiply connected surfaces ..."
            << endl;

        const polyBoundaryMesh& patches = mesh.boundaryMesh();

        // Non-manifold points
        pointSet points
        (
            mesh,
            "nonManifoldPoints",
            mesh.nPoints()/100
        );

        Pout.setf(ios_base::left);

        Pout<< "    "
            << setw(20) << "Patch"
            << setw(9) << "Faces"
            << setw(9) << "Points"
            << setw(34) << "Surface topology";
        if (allGeometry)
        {
            Pout<< " Bounding box";
        }
        Pout<< endl;

        forAll(patches, patchI)
        {
            const polyPatch& pp = patches[patchI];

                Pout<< "    "
                    << setw(20) << pp.name()
                    << setw(9) << pp.size()
                    << setw(9) << pp.nPoints();


            primitivePatch::surfaceTopo pTyp = pp.surfaceType();

            if (pp.empty())
            {
                Pout<< setw(34) << "ok (empty)";
            }
            else if (pTyp == primitivePatch::MANIFOLD)
            {
                if (pp.checkPointManifold(true, &points))
                {
                    Pout<< setw(34) << "multiply connected (shared point)";
                }
                else
                {
                    Pout<< setw(34) << "ok (closed singly connected)";
                }

                // Add points on non-manifold edges to make set complete
                pp.checkTopology(false, &points);
            }
            else
            {
                pp.checkTopology(false, &points);

                if (pTyp == primitivePatch::OPEN)
                {
                    Pout<< setw(34) << "ok (non-closed singly connected)";
                }
                else
                {
                    Pout<< setw(34) << "multiply connected (shared edge)";
                }
            }

            if (allGeometry)
            {
                const pointField& pts = pp.points();
                const labelList& mp = pp.meshPoints();

                if (returnReduce(mp.size(), sumOp<label>()) > 0)
                {
                    boundBox bb(point::max, point::min);
                    forAll (mp, i)
                    {
                        bb.min() = min(bb.min(), pts[mp[i]]);
                        bb.max() = max(bb.max(), pts[mp[i]]);
                    }
                    reduce(bb.min(), minOp<vector>());
                    reduce(bb.max(), maxOp<vector>());
                    Pout<< ' ' << bb;
                }
            }
            Pout<< endl;
        }

        if (points.size())
        {
            Pout<< "  <<Writing " << points.size()
                << " conflicting points to set "
                << points.name() << endl;

            points.instance() = mesh.pointsInstance();
            points.write();
        }

        //Pout.setf(ios_base::right);
    }

    // Force creation of all addressing if requested.
    // Errors will be reported as required
    if (allTopology)
    {
        mesh.cells();
        mesh.faces();
        mesh.edges();
        mesh.points();
        mesh.faceOwner();
        mesh.faceNeighbour();
        mesh.cellCells();
        mesh.edgeCells();
        mesh.pointCells();
        mesh.edgeFaces();
        mesh.pointFaces();
        mesh.cellEdges();
        mesh.faceEdges();
        mesh.pointEdges();
    }

    return noFailedChecks;
}
