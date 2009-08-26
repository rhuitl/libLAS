
// god-awful hack because of GDAL/GeoTIFF's shitty include structure
#define CPL_SERV_H_INCLUDED

#include "oci_wrapper.h"

#include <liblas/cstdint.hpp>
#include <liblas/detail/utility.hpp>

#include <liblas/laspoint.hpp>
#include <liblas/lasreader.hpp>
#include <liblas/lasheader.hpp>
#include <liblas/index/index.hpp>

#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <exception>
#include <algorithm>
#include <cctype>

#ifdef HAVE_SPATIALINDEX
#include <spatialindex/SpatialIndex.h>
#endif

#include <oci.h>

using namespace std;
using namespace liblas;

#ifdef _WIN32
#define compare_no_case(a,b,n)  _strnicmp( (a), (b), (n) )
#else
#define compare_no_case(a,b,n)  strncasecmp( (a), (b), (n) )
#endif




std::istream* OpenInput(std::string filename) 
{
    std::ios::openmode const mode = std::ios::in | std::ios::binary;
    std::istream* istrm;
    if (compare_no_case(filename.c_str(),"STDIN",5) == 0)
    {
        istrm = &std::cin;
    }
    else 
    {
        istrm = new std::ifstream(filename.c_str(), mode);
    }
    
    if (!istrm->good())
    {
        delete istrm;
        throw std::runtime_error("Reading stream was not able to be created");
        exit(1);
    }
    return istrm;
}

OWStatement* Run(OWConnection* connection, ostringstream& command) 
{
    OWStatement* statement = 0;
    statement = connection->CreateStatement(command.str().c_str());
    
    if (statement->Execute() == false) {
        delete statement;
        return 0;
    }
    
    return statement;    
}

bool Cleanup(OWConnection* connection, const char* tableName)
{
    ostringstream oss;
    OWStatement* statement = 0;
    
    oss << "DELETE FROM " << tableName;
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");

    oss << "DROP TABLE " << tableName;
    cout << oss.str() << endl;
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");    
    
    oss << "DROP TABLE BLKTAB ";
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");
    
    oss << "DROP TABLE RES ";
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");

    oss << "DROP TABLE INPTAB ";
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");

    oss << "DROP TABLE LIDAR_DATA";
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");    

    oss << "DELETE FROM USER_SDO_GEOM_METADATA WHERE TABLE_NAME='"<< tableName << "'";
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");

    return true;
    
    
}


bool CreateTable(OWConnection* connection, const char* tableName)
{
    ostringstream oss;
    OWStatement* statement = 0;
    
    oss << "CREATE TABLE "<< tableName <<" (OBJ_ID NUMBER, BLK_ID NUMBER, "
                                         " BLK_EXTENT MDSYS.SDO_GEOMETRY, BLK_DOMAIN MDSYS.SDO_ORGSCL_TYPE,"
                                         " PCBLK_MIN_RES NUMBER, PCBLK_MAX_RES NUMBER, NUM_POINTS NUMBER, "
                                         " NUM_UNSORTED_POINTS NUMBER, PT_SORT_DIM NUMBER, POINTS BLOB)";
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");

    return true;

}

bool DeleteTable(OWConnection* connection, const char* tableName, const char* cloudTableName, const char* cloudColumnName)
{
    ostringstream oss;
    OWStatement* statement = 0;

    oss << "DELETE from " <<cloudTableName;
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");

oss << "declare\n"
"begin \n"
"  mdsys.sdo_pc_pkg.drop_dependencies('"<<cloudTableName<<"', '"<<cloudColumnName<<"');"
"end;";
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");
    
    oss << "DROP TABLE "<< tableName ;
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");

    // Oracle upper cases the table name when inserting it in the 
    // USER_SDO_GEOM_METADATA.  We'll use std::transform to do it. 
    // See http://forums.devx.com/showthread.php?t=83058 for the 
    // technique
    string table(tableName);
    std::transform(table.begin(), table.end(), table.begin(), static_cast < int(*)(int) > (toupper));
    oss << "DELETE FROM USER_SDO_GEOM_METADATA WHERE TABLE_NAME='"<<table<<"'" ;
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");    
    return true;

}
bool GetPointData(LASPoint const& p, bool bTime, std::vector<liblas::uint8_t>& point_data)
{
    // This function returns an array of bytes describing the 
    // x,y,z and optionally time values for the point.  

    point_data.clear();

    double x = p.GetX();
    double y = p.GetY();
    double z = p.GetZ();
    double t = p.GetTime();

    liblas::uint8_t* x_b =  reinterpret_cast<liblas::uint8_t*>(&x);
    liblas::uint8_t* y_b =  reinterpret_cast<liblas::uint8_t*>(&y);
    liblas::uint8_t* z_b =  reinterpret_cast<liblas::uint8_t*>(&z);

    liblas::uint8_t* t_b =  reinterpret_cast<liblas::uint8_t*>(&t);

    // doubles are 8 bytes long.  For each double, push back the 
    // byte.  We do this for all four values (x,y,z,t)

    // // little-endian
    // for (int i=0; i<sizeof(double); i++) {
    //     point_data.push_back(y_b[i]);
    // }
    // 

    // big-endian
    for (int i = sizeof(double) - 1; i >= 0; i--) {
        point_data.push_back(x_b[i]);
    }

    for (int i = sizeof(double) - 1; i >= 0; i--) {
        point_data.push_back(y_b[i]);
    }   

    for (int i = sizeof(double) - 1; i >= 0; i--) {
        point_data.push_back(z_b[i]);
    }

    
    if (bTime)
    {
        for (int i = sizeof(double) - 1; i >= 0; i--) {
            point_data.push_back(t_b[i]);
        }

    }

    return true;
}
bool GetResultData(const LASQueryResult& result, LASReader* reader, std::vector<liblas::uint8_t>& data, int nDimension)
{
    list<SpatialIndex::id_type> const& ids = result.GetIDs();


    // d 8-byte IEEE  big-endian doubles, where d is the PC_TOT_DIMENSIONS value
    // 4-byte big-endian integer for the BLK_ID value
    // 4-byte big-endian integer for the PT_ID value
    
    bool bTime = false;
    if (nDimension == 4)
    {
        bTime = true;
    }
    
    data.clear();
    
    list<SpatialIndex::id_type>::const_iterator i;
    vector<liblas::uint8_t>::iterator pi;
    
    liblas::uint32_t block_id = result.GetID();

    std::vector<liblas::uint8_t> point_data;
    
    for (i=ids.begin(); i!=ids.end(); i++) 
    {
        SpatialIndex::id_type id = *i;

        bool doRead = reader->ReadPointAt(id);
        if (doRead) {
            LASPoint const& p = reader->GetPoint();

            // d 8-byte IEEE  big-endian doubles, where d is the PC_TOT_DIMENSIONS value
            bool gotdata = GetPointData(p, bTime, point_data);

            std::vector<liblas::uint8_t>::const_iterator d;
            for (d = point_data.begin(); d!=point_data.end(); d++) {
                data.push_back(*d);
            }

            liblas::uint8_t* id_b = reinterpret_cast<liblas::uint8_t*>(&id);
            liblas::uint8_t* block_b = reinterpret_cast<liblas::uint8_t*>(&block_id);
            
            // 4-byte big-endian integer for the BLK_ID value
            for (int i =  sizeof(liblas::uint32_t) - 1; i >= 0; i--) {
                data.push_back(block_b[i]);
            }
            
            // 4-byte big-endian integer for the PT_ID value
            for (int i =  sizeof(liblas::uint32_t) - 1; i >= 0; i--) {
                data.push_back(id_b[i]);
            }
            

        }
    }

    return true;
}

bool InsertBlock(OWConnection* connection, const LASQueryResult& result, int srid, LASReader* reader, const char* tableName)
{
    ostringstream oss;

    list<SpatialIndex::id_type> const& ids = result.GetIDs();
    const SpatialIndex::Region* b = result.GetBounds();
    liblas::uint32_t num_points =ids.size();
    ostringstream oss_geom;
    
    oss_geom.setf(std::ios_base::fixed, std::ios_base::floatfield);
    oss_geom.precision(2);
    oss_geom << "mdsys.sdo_geometry(2003,"<<srid<<", null,"
              "mdsys.sdo_elem_info_array(1,1003,3),"
              "mdsys.sdo_ordinate_array("<< b->getLow(0) <<","<<b->getLow(1)<<","<<b->getHigh(0) <<","<<b->getHigh(1)<<"))";
    oss << "INSERT INTO "<< tableName << "(OBJ_ID, NUM_POINTS, BLK_EXTENT, POINTS) VALUES ( " 
                         << result.GetID() <<"," << num_points << ", " << oss_geom.str() <<", EMPTY_BLOB())";

    OWStatement* statement = 0;
    statement = connection->CreateStatement(oss.str().c_str());

    if (statement->Execute() == false) {
        delete statement;
        return false;
    }
    
    delete statement; statement = 0;
    oss.str("");
    
    oss << "SELECT POINTS FROM " << tableName << " WHERE OBJ_ID=" << result.GetID() << " FOR UPDATE";
    
    // we only expect one blob to come back
    OCILobLocator** locator =(OCILobLocator**) VSIMalloc( sizeof(OCILobLocator*) * 1 );

    statement = connection->CreateStatement(oss.str().c_str());
    statement->Define( locator, 1 ); // fetch one blob
    

    if( statement->Execute() == false )
    {
        std::cout << "Unable to execute statement!" << std::endl;
        delete statement;
        return false;
    }

    if( statement->Fetch( 1 ) == false )
    {
        std::cout << "Unable to fetch POINTS blob!" << std::endl;
        delete statement;
        return false;
    }
    
    std::vector<liblas::uint8_t> data;
    bool gotdata = GetResultData(result, reader, data, 3);
    if (! gotdata) throw std::runtime_error("unable to fetch point data byte array");
    
    liblas::uint32_t wroteblob = statement->WriteBlob(  locator[0],
                                                        (void*)&(data[0]),
                                                        data.size());
    
    if (! wroteblob) throw std::runtime_error("No blob bytes could be written!");
//select dbms_lob.getlength(points) from TO_core_last_clip
    OWStatement::Free(locator, 1);

    delete statement;


    
    return true;

}

bool CreateSDOEntry(OWConnection* connection, const char* tableName, LASQuery* query, int srid)
{
    ostringstream oss;
    OWStatement* statement = 0;
    // SpatialIndex::Region* b = query->bounds;
    ostringstream oss_geom;
    
    oss.setf(std::ios_base::fixed, std::ios_base::floatfield);
    oss.precision(2);
     
//     code = """
// INSERT INTO user_sdo_geom_metadata VALUES (
//     'foo',
//     'blk_extent',
//     MDSYS.SDO_DIM_ARRAY(
// MDSYS.SDO_DIM_ELEMENT('X', 630250.000000, 630500.000000, 0.05),
// MDSYS.SDO_DIM_ELEMENT('Y', 4834500, 4834750, 0.05)),
//     8307)
// """
    oss <<  "INSERT INTO user_sdo_geom_metadata VALUES ('" << tableName <<
            "','blk_extent', MDSYS.SDO_DIM_ARRAY(" 
            "MDSYS.SDO_DIM_ELEMENT('X', "<< query->bounds.getLow(0) <<","<< query->bounds.getHigh(0)<<",0.05),"
            "MDSYS.SDO_DIM_ELEMENT('Y', "<< query->bounds.getLow(1) <<","<< query->bounds.getHigh(1)<<",0.05)),"
            << srid << ")";
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");
    
    return true;
        
}

bool CreateBlockIndex(OWConnection* connection, const char* tableName)
{
    ostringstream oss;
    OWStatement* statement = 0;
    
    oss << "CREATE INDEX "<< tableName <<"_cloud_idx on "<<tableName<<"(blk_extent) INDEXTYPE IS MDSYS.SPATIAL_INDEX" ;
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");
    
    return true;
        
}

bool CreatePCEntry( OWConnection* connection, 
                    LASQuery* query, 
                    const char* blkTableName, 
                    const char* pcTableName, 
                    const char* cloudColumnName,
                    int nDimension, 
                    int srid,
                    int blk_capacity)
{
    ostringstream oss;
    OWStatement* statement = 0;

    oss.setf(std::ios_base::fixed, std::ios_base::floatfield);
    oss.precision(2);
    
oss << "declare\n"
"  pc sdo_pc;\n"
"begin\n"
"  -- Initialize the Point Cloud object.\n"
"  pc := sdo_pc_pkg.init( \n"
"          '"<< pcTableName<<"', -- Table that has the SDO_POINT_CLOUD column defined\n"
"          '"<< cloudColumnName<<"',   -- Column name of the SDO_POINT_CLOUD object\n"
"          '"<<blkTableName<<"', -- Table to store blocks of the point cloud\n"
"           'blk_capacity="<<blk_capacity<<"', -- max # of points per block\n"
"           mdsys.sdo_geometry(2003, "<<srid<<", null,\n"
"              mdsys.sdo_elem_info_array(1,1003,3),\n"
"              mdsys.sdo_ordinate_array(\n"
<< query->bounds.getLow(0) << ","
<< query->bounds.getLow(1) << ","
<< query->bounds.getHigh(0) << ","
<< query->bounds.getHigh(1) << ")),  -- Extent\n"
"     0.5, -- Tolerance for point cloud\n"
"           "<<nDimension<<", -- Total number of dimensions\n"
"           null);\n"

"  -- Insert the Point Cloud object  into the \"base\" table.\n"
"  insert into hobu values (pc);\n"
"end;\n";
    
    statement = Run(connection, oss);
    if (statement != 0) delete statement; else return false;
    oss.str("");
    
    return true;
}
void usage() {}

// select sdo_pc_pkg.to_geometry(a.points, a.num_points, 3, 8307) from NACHES_BAREEARTH_BLOCK1 a where a.obj_id= 8907
int main(int argc, char* argv[])
{

    std::string input;
    std::string connection;
    std::string username;
    std::string password;
    std::string instance;
    std::string block_table_name;
    std::string point_cloud_name("CLOUD");
    
    bool bDropTable = false;
    liblas::uint32_t nCapacity = 10000;
    double dFillFactor = 0.99;
    int srid = 8307;
    
    for (int i = 1; i < argc; i++)
    {
        if (    strcmp(argv[i],"-h") == 0 ||
                strcmp(argv[i],"--help") == 0
            )
        {
            usage();
            exit(0);
        }
        else if (   strcmp(argv[i],"--input") == 0  ||
                    strcmp(argv[i],"-input") == 0   ||
                    strcmp(argv[i],"-i") == 0       ||
                    strcmp(argv[i],"-in") == 0
                )
        {
            i++;
            input = std::string(argv[i]);
        }
        else if (   strcmp(argv[i],"--overwrite") == 0  ||
                    strcmp(argv[i],"-drop") == 0   ||
                    strcmp(argv[i],"-d") == 0 
                )
        {
            i++;
            bDropTable=true;
        }
        else if (   strcmp(argv[i],"--blk_capacity") == 0  ||
                    strcmp(argv[i],"--capacity") == 0   ||
                    strcmp(argv[i],"-c") == 0 
                )
        {
            i++;
            nCapacity=atoi(argv[i]);
        }
        else if (   strcmp(argv[i],"--fill") == 0  ||
                    strcmp(argv[i],"-fill") == 0   ||
                    strcmp(argv[i],"-f") == 0 
                )
        {
            i++;
            dFillFactor=atof(argv[i]);
        }
        else if (   strcmp(argv[i],"--srid") == 0  ||
                    strcmp(argv[i],"-srid") == 0   ||
                    strcmp(argv[i],"-s") == 0 
                )
        {
            i++;
            srid=atoi(argv[i]);
        }
        else if (   strcmp(argv[i],"--cloud-column-name") == 0  ||
                    strcmp(argv[i],"-cn") == 0  
                )
        {
            i++;
            point_cloud_name = std::string(argv[i]);
        }
        else if (input.empty())
        {
            input = std::string(argv[i]);
        }
        else if (connection.empty())
        {
            connection = std::string(argv[i]);
        }
        else 
        {
            usage();
            exit(1);
        }
    }


    string::size_type slash_pos = connection.find("/",0);
    username = connection.substr(0,slash_pos);
    string::size_type at_pos = connection.find("@",slash_pos);
    std::cout <<"slash_pos: " << slash_pos << " at_pos: " << at_pos<<std::endl;
    password = connection.substr(slash_pos+1, at_pos-slash_pos-1);
    instance = connection.substr(at_pos+1);
    std::cout << "Connecting with username: " << username << " password: "<< password<< " instance: " << instance << std::endl;    

    // OCI_SUCCESS_WITH_INFO error, which according to google relates to 
    // a warning related to expired or expiring passwords needs to be 
    // handled in the oracle wrapper.
    
    // Create the index before connecting to Oracle.  That way we don't heartbeat
    // the server while we're cruising through the file(s).
    
    // OS X RAMDISK configuration
    // http://www.macosxhints.com/article.php?story=20090222092827145
    
    // Obj_id is serial for each row in the block table
    // blk_id is the index leaf node id (this is currently being written incorrectly)
    OWConnection* con = new OWConnection(username.c_str(),password.c_str(),instance.c_str());
    if (con->Succeeded()) {
        std::cout <<"Oracle connection succeded" << std::endl;
    } else {
        std::cout <<"Oracle connection failed" << std::endl; exit(1);
    }


    std::istream* istrm;
    try {
            istrm = OpenInput(input);
    } catch (std::exception const& e)
    {
        std::cout << e.what() << std::endl;
        std::cout << "exiting..." << std::endl;
        exit(-1);
    }

    
    
    // change filename foo.las -> foo for an appropriate
    // block tablename for oracle... must be less than 30 characters
    // and no extraneous characters.
    
    // We need an option for the user to specify the blk tablename
    string::size_type dot_pos = input.find_first_of(".");
    string table_name = input.substr(0,dot_pos);
    
    if (bDropTable) DeleteTable(con, table_name.c_str(), "HOBU", "CLOUD");
    CreateTable(con, table_name.c_str());

    LASReader* reader = new LASReader(*istrm);
    LASIndexDataStream* idxstrm = new LASIndexDataStream(reader);
    // reader->Reset();

    LASIndex* idx = new LASIndex(input);
    idx->SetType(LASIndex::eExternalIndex);
    idx->SetLeafCapacity(nCapacity);
    idx->SetFillFactor(dFillFactor);
    idx->Initialize(*idxstrm);

    LASQuery* query = new LASQuery;
    idx->Query(*query);    

    if (idx != 0) delete idx;
    if (reader != 0) delete reader;
    if (istrm != 0 ) delete istrm;
    
    std::list<LASQueryResult>& results = query->GetResults();
    
    std::list<LASQueryResult>::const_iterator i;
    
    std::istream* istrm2;
    istrm2 = OpenInput(input);
    LASReader* reader2 = new LASReader(*istrm2);
    for (i=results.begin(); i!=results.end(); i++)
    {
        bool inserted = InsertBlock(con, *i, srid, reader2, table_name.c_str());
    }
    

    CreateSDOEntry(con, table_name.c_str(), query, srid );
    CreateBlockIndex(con, table_name.c_str());

    CreatePCEntry(  con, 
                    query, 
                    table_name.c_str(),
                    "HOBU",
                    "CLOUD",
                    3, // we're assuming 3d for now
                    srid,
                    nCapacity);
//    Cleanup(con, "base");

 // int   iCol = 0;
 //    char  szField[OWNAME];
 //    int   hType = 0;
 //    int   nSize = 0;
 //    int   nPrecision = 0;
 //    signed short nScale = 0;
 // 
 //    char szColumnList[OWTEXT];
 //    szColumnList[0] = '\0';
 //    OCIParam* phDesc = NULL;
 // 
 //    const char* pszVATName="base";
 //    phDesc = con->GetDescription( (char*) pszVATName );
 //    while( con->GetNextField(
 //                phDesc, iCol, szField, &hType, &nSize, &nPrecision, &nScale ) )
 //    {
 //        printf("field ... %s",szField);
 //        switch( hType )
 //        {
 //            case SQLT_FLT:
 //                printf("float...\n");
 //                break;
 //            case SQLT_NUM:
 //                printf ("number...\n");
 //                break;
 //            case SQLT_CHR:
 //            case SQLT_AFC:
 //            case SQLT_DAT:
 //            case SQLT_DATE:
 //            case SQLT_TIMESTAMP:
 //            case SQLT_TIMESTAMP_TZ:
 //            case SQLT_TIMESTAMP_LTZ:
 //            case SQLT_TIME:
 //            case SQLT_TIME_TZ:
 //                printf ("character...\n");
 //                break;
 //            default:
 //                CPLDebug("GEORASTER", "VAT (%s) Column (%s) type (%d) not supported"
 //                    "as GDAL RAT", pszVATName, szField, hType );
 //                break;
 //        }
 //        // strcpy( szColumnList, CPLSPrintf( "%s substr(%s,1,%d),",
 //        //     szColumnList, szField, MIN(nSize,OWNAME) ) );
 // 
 //        iCol++;
 //    }



}