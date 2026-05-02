// xbe.cpp: XBE file parser. Reads the XBE header, certificate, section
// table, and import table; pulls out the title id, title name, ratings,
// alt-title-ids, embedded $$XTIMAGE title image, and the icon resource.
// Used by the title scanner to populate cache.ini / Icons.ini.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "runner.h"

#include "xbe.h"



char  g_szConvertThinText[ 1025 ];
WCHAR g_szConvertWideText[ 1025 ];


char * StrReplace( char * szString, const char chFind, const char chReplace, int * piNumReplaced = ( int * )NULL )
{
  int iPos;
  iPos = ( int )0;
  if ( piNumReplaced != ( int * )NULL )
    {
    *piNumReplaced = ( int )0;
    }
  while ( szString[ iPos ] != ( char )'\0' )
    {
    if ( szString[ iPos ] == ( char )chFind )
      {
      szString[ iPos ] = ( char )chReplace;
      if ( piNumReplaced != ( int * )NULL )
        {
        *piNumReplaced = ( *piNumReplaced + 1 );
        }
      }
    iPos++;
    }
  return ( char * )szString;
}

char * Trim( char * szString )
{
  char  * pszString;
  int     iLen;
  pszString = ( char * )szString;

  while ( ( *pszString == ( char )' ' ||
            *pszString == ( char )'\t' ) &&
          pszString++ );

  iLen = ( int )strlen( pszString );
  while ( iLen-- > 0 &&
          ( *( pszString + iLen ) == ( char )' ' ||
            *( pszString + iLen ) == ( char )'\t' ) )
    {
    *( pszString + iLen ) = ( char )'\0';
    }
  return ( char * )pszString;
}

WCHAR * GetWideText( char * szThin, int iLen )
{ 
	if ( iLen > 0 )
    {
		g_szConvertWideText[ iLen ] = ( WCHAR )'\0';
    }
	MultiByteToWideChar( CP_ACP, 0, szThin, ( -1 ), g_szConvertWideText, 1024 );
	return ( WCHAR * )g_szConvertWideText;
}

char * GetThinText( WCHAR * szWide, int iLen = ( int )( -1 ) )
{ 
	memset( g_szConvertThinText, 0, sizeof( g_szConvertThinText ) );
	WideCharToMultiByte( CP_ACP, 0, szWide, ( -1 ), g_szConvertThinText, 1024, NULL, NULL );
	return ( char * )g_szConvertThinText;
}

WCHAR * ConvertString( char *text )
{
	static WCHAR temp[16*1024];
	if( ! text )
		return 0;

	mbstowcs( temp, text, 16*1024 );
	return temp;
}

CXBExecutable::CXBExecutable( void )
{
	m_pTitleImageTexture = NULL;
	m_iHeaderSize = ( 256 * 1024 );
	m_pHeader = ( char * )NULL;
	m_iImageSize = ( 256 * 1024 );
	m_pImage = ( char * )NULL;
	m_iTitleImageActualSize = 0;
	Clear( );
}


CXBExecutable::~CXBExecutable( void )
{
	Clear( );
	if ( m_pHeader != ( char * )NULL )
		{
			free( m_pHeader );
			m_pHeader = ( char * )NULL;
		}

	if ( m_pImage != ( char * )NULL )
		{
			free( m_pImage );
			m_pImage = ( char * )NULL;
		}
}


int CXBExecutable::Clear( void )
{
	// Clear information
	memset( m_szFileName, 0, sizeof( m_szFileName ) );

	m_ulTitleId = ( unsigned long )0;
	m_ulMediaFlag = ( unsigned long )0;
	m_ulGameRegion = ( unsigned long )0;

	memset( m_szInternalName, 0, sizeof( m_szInternalName ) );
	memset( m_szTitleName,    0, sizeof( m_szTitleName ) );
	memset( &m_XBEInfo,       0, sizeof( m_XBEInfo ) );

	// Release texture (if any)
	if ( m_pTitleImageTexture )
		{
		m_pTitleImageTexture->Release( );
		m_pTitleImageTexture = NULL;
		}

	// Return OK
	return 1;
}


int CXBExecutable::ReadFile( const char * szFileName, const bool bGetTitleImage, const bool bGetAlternativeImage )
{
	HANDLE	  hFile;
	DWORD		  dwRead;

	int		  iNumSections;

	char		* pszSectionName;
	char		* pszPos;

	// Clear information
	Clear( );

	// Do we have a header memory area?
	if ( m_pHeader == ( char * )NULL )
		{
		// No header area. Allocate memory for header
		if ( ( m_pHeader = ( char * )malloc( m_iHeaderSize ) ) == ( char * )NULL )
			{
			// Unable to allocate memory. Return not OK
			return 0;
			}
		}

	// Copy path
	strcpy( m_szFileName, szFileName );

	if( GetFileAttributes( szFileName ) != -1 )
	{
		// Open the local file
		hFile = CreateFile( szFileName,
							GENERIC_READ,
							0,
							NULL,
							OPEN_EXISTING,
							FILE_ATTRIBUTE_NORMAL,
							NULL );
	}
	else
		hFile = ( HANDLE )INVALID_HANDLE_VALUE;
	// Did we manage to open the file?
	if ( hFile == ( HANDLE )INVALID_HANDLE_VALUE )
		{
		// Unable to open file. Return not OK
		return 0;
		}

	// Read the header
	if ( ::ReadFile( hFile,
					 m_pHeader,
					 min( m_iHeaderSize,
					 ( int )GetFileSize( hFile, ( LPDWORD )NULL ) ),
					 &dwRead,
					 NULL ) == ( BOOL )TRUE )
		{
		// Header read. Copy information about header
		memcpy( &m_XBEInfo.Header, m_pHeader, sizeof( m_XBEInfo.Header ) );
		}

	// Valid?
	if ( Valid( ) )
		{
		// Header read. Copy information about certificate
		memcpy( &m_XBEInfo.Certificate, ( m_pHeader + m_XBEInfo.Header.sizeof_image_header ), sizeof( m_XBEInfo.Certificate ) );

		// Copy title id.
		m_ulTitleId = m_XBEInfo.Certificate.titleid;

		// Copy media flag
		m_ulMediaFlag = m_XBEInfo.Certificate.allowed_media;

		// Copy game region code
		m_ulGameRegion = m_XBEInfo.Certificate.game_region;

		// Get name
		strcpy( m_szInternalName, GetThinText( (WCHAR*)m_XBEInfo.Certificate.title_name, 40 ) );










		// Do we have an internal name?
		if ( *m_szInternalName == ( char )'\0' )
			{
			// Read title name
			ReadTitleName( );

			// Did we get a title name?
			if ( *m_szTitleName != ( char )'\0' )
				{
				// Copy title name
				strcpy( m_szInternalName, m_szTitleName );
				}
			else
				{
				// Copy file name
				strcpy( m_szBuffer, szFileName );

				// Find last '\'
				if ( ( pszPos = ( char * )strrchr( m_szBuffer, ( int )'\\' ) ) != ( char * )NULL )
					{
					// Terminate string
					*pszPos = ( char )'\0';

					// Find the last '\' again
					if ( ( pszPos = ( char * )strrchr( m_szBuffer, ( int )'\\' ) ) != ( char * )NULL )
						{
						// Set folder name as internal name
						strcpy( m_szInternalName, ( char * )( pszPos + 1 ) );
						}
					}

				// Do we have an internal name?
				if ( *m_szInternalName == ( char )'\0' )
					{
					// Copy default name
					strcpy( m_szInternalName, "<unknown>" );
					}
				}
			}
		}

	// Valid?
	if ( Valid( ) &&
		 bGetTitleImage &&
		 TheseusGetD3DDev() != NULL )
		{
		// Check when (if so) to read the alternative image
		//if ( bGetAlternativeImage == true )
		//	{
		//	// Read alternative title image
		//	ReadAltTitleImage( );
		//	}

		// Do we have a title image?
		if ( m_pTitleImageTexture == NULL )
			{
			// Initialize
			iNumSections = m_XBEInfo.Header.sections;

			// Position at the first section
			m_XBEInfo.pSection_Header = ( _xbe_info_::section_header * )( m_pHeader + ( m_XBEInfo.Header.section_headers_addr -
																						m_XBEInfo.Header.base ) );

			// Sections read. Parse all the section headers
			while ( iNumSections-- > ( int )0 )
				{
				// Is this section an inserted file?
				if ( ( int )m_XBEInfo.pSection_Header->Flags.inserted_file == ( int )1 )
					{
					// Inserted file. Position at the name of the section
					pszSectionName = ( m_pHeader + ( m_XBEInfo.pSection_Header->section_name_addr -
													 m_XBEInfo.Header.base ) );

					// Title image?
					if ( strcmp( pszSectionName, "$$XTIMAGE" ) == ( int )0 )
						{
						// Do we have an image memory area?
						if ( m_pImage == ( char * )NULL )
							{
							// No image area. Allocate memory for image
							if ( ( m_pImage = ( char * )malloc( m_iImageSize ) ) == ( char * )NULL )
								{
								// Unable to allocate memory. Break out of loop
								break;
								}
							}

						// Position at 'title image' in the XBE itself
						if ( SetFilePointer( hFile,
											 m_XBEInfo.pSection_Header->raw_addr,
											 NULL,
											 FILE_BEGIN ) == ( DWORD )0xFFFFFFFF )
							{
							// Unable to set file position. Break out of loop
							break;
							}

						// Read the image
						if ( ::ReadFile( hFile,
										 m_pImage,
										 min( m_iImageSize,
										 ( int )m_XBEInfo.pSection_Header->sizeof_raw ),
										 &dwRead,
										 NULL ) == ( BOOL )FALSE )
							{
							// Image not read. Break out of loop
							break;
							}

						// Preserve the actual byte count so callers writing
						// the image back out (e.g. UDATA TitleImage.xbx) can
						// use the right size, not the allocation cap.
						m_iTitleImageActualSize = ( int )dwRead;

						// Get title image
						ReadTitleImage( hFile );

						// Break out of loop
						break;
						}
					}

				// Position at the next section
				m_XBEInfo.pSection_Header++;
				}
			}

		// Do we have a title image?
		//if ( bGetAlternativeImage &&
		//	 m_pTitleImageTexture == NULL  )
		//	{
		//	// Read alternative title image
		//	ReadAltTitleImage( );
		//	}
		}

	// Close file
	CloseHandle( hFile );

	// Valid?
	if ( Valid( ) )
		{
		// Return OK
		return 1;
		}

	// Return not OK
	return 0;
}


int CXBExecutable::SaveFile( void )
{
	HANDLE  hFile;
	DWORD   dwWritten;

	// Do we have a valid header?
	if ( !Valid( ) )
		{
		// Not a valid header. Return not OK
		return 0;
		}

	// Open the local file
	hFile = ::CreateFile( m_szFileName,
						  GENERIC_WRITE,
						  0,
						  NULL,
						  OPEN_EXISTING,
						  FILE_ATTRIBUTE_NORMAL,
						  NULL );

	// Did we manage to open the file?
	if ( hFile == ( HANDLE )INVALID_HANDLE_VALUE )
		{
		// Unable to open file. Return not OK
		return 0;
		}

	// Position at the certificate
	if ( SetFilePointer( hFile,
						 m_XBEInfo.Header.sizeof_image_header,
						 NULL,
						 FILE_BEGIN ) == ( DWORD )0xFFFFFFFF )
		{
		// Unable to read the certificate. Clear header
		memset( &m_XBEInfo, 0, sizeof( m_XBEInfo ) );
		}
	// Write the certificate
	else if ( WriteFile( hFile,
						 &m_XBEInfo.Certificate,
						 sizeof( m_XBEInfo.Certificate ),
						 &dwWritten,
						 NULL ) == ( BOOL )FALSE )
		{
		// Unable to read the certificate. Clear header
		memset( &m_XBEInfo, 0, sizeof( m_XBEInfo ) );
		}

	// Close file
	CloseHandle( hFile );

	// Valid?
	if ( Valid( ) )
		{
		// Return OK
		return 1;
		}

	// Return not OK
	return 0;
}


const char * CXBExecutable::SetInternalName( const char * szInternalName )
{
	int iLoop;

	// Clear title
	memset( m_szInternalName, 0, sizeof( m_szInternalName ) );

	// Copy up to 40 characters
	strncpy( m_szInternalName, szInternalName, 40 );

	// Copy title
	for ( iLoop = ( int )0; iLoop < ( int )40; iLoop++ )
		{
		// Copy character
		m_XBEInfo.Certificate.title_name[ iLoop ] = ( uint16 )szInternalName[ iLoop ];
		}

	// Return reference to the title
	return ( const char * )m_szInternalName;
}


int CXBExecutable::ReadTitleImage( HANDLE hFile )
{
	LPDIRECT3DDEVICE8   pd3dDevice;

	XPR_FILE_HEADER     xprh;
	BOOL                bSuccess;

	D3DSURFACE_DESC     desc;
	D3DLOCKED_RECT      rect;

	D3DFORMAT			fmtTexture;
	LPDIRECT3DSURFACE8  pTempSurface;
	LPDIRECT3DSURFACE8  pDestSurface;

	// Copy reference
	pd3dDevice = TheseusGetD3DDev();

	// Release previous texture (if any)
	if ( m_pTitleImageTexture )
		{
		m_pTitleImageTexture->Release( );
		m_pTitleImageTexture = NULL;
		}

	// Initialize
	pTempSurface = NULL;
	pDestSurface = NULL;

	// Copy the header
	memcpy( &xprh, m_pImage, sizeof( xprh ) );

	// Check the magic number
	bSuccess = ( xprh.Type.dwXPRMagic == _XPR_HEADER_MAGIC );

	// Success?
	if ( !bSuccess )
		{
		// Not a valid XPR texture. Try to create texture
		bSuccess = SUCCEEDED( D3DXCreateTextureFromFileInMemory( pd3dDevice,
																 m_pImage,
																 min( m_iImageSize,
																	  ( int )m_XBEInfo.pSection_Header->sizeof_raw ),
																 &m_pTitleImageTexture ) );

		// Return status
		return ( bSuccess ? 1 : 0 );
		}

	// Copy texture format
	fmtTexture = ( D3DFORMAT )xprh.btTextureFormat;

	// Check texture format
	if ( xprh.btTextureFormat == ( D3DFORMAT )D3DFMT_R8G8B8A8 )
		{
		// Doesn't support this very well. Convert it to a A8 R8 G8 B8 texture instead
		fmtTexture = ( D3DFORMAT )D3DFMT_A8R8G8B8;
		}

	// Create image surface
	bSuccess = SUCCEEDED( pd3dDevice->CreateImageSurface( ( UINT )pow( 2, xprh.btTextureWidth ),
														  ( UINT )pow( 2, xprh.btTextureHeight ),
														  fmtTexture,
														  &pTempSurface ) );

	// Success?
	if ( bSuccess )
		{
		// Get description
		pTempSurface->GetDesc( &desc );

		// Lock rectangle
		bSuccess = SUCCEEDED( pTempSurface->LockRect( &rect, NULL, ( D3DLOCK_READONLY | D3DLOCK_NOOVERWRITE ) ) );
		}

	// Success?
	if ( bSuccess )
		{
		// Check texture format
		if ( xprh.btTextureFormat == D3DFMT_R8G8B8A8 )
			{
			// Doesn't support this very well. Convert it to a A8 R8 G8 B8 texture instead
			DWORD	dwLoop;
			DWORD * pdwData;

			FLOAT	fR;
			FLOAT	fG;
			FLOAT	fB;
			FLOAT	fA;

			// Position at the data
			pdwData = ( DWORD * )( char * )( m_pImage + xprh.dwHeaderSize );

			// Parse all the bytes
			for ( dwLoop = 0; dwLoop < desc.Size; dwLoop += 4, pdwData++ )
				{
				// Get source pixel
				fR = ( ( ( ( *pdwData ) & 0xFF000000 ) >> 24L ) / 255.0f );
				fG = ( ( ( ( *pdwData ) & 0x00FF0000 ) >> 16L ) / 255.0f );
				fB = ( ( ( ( *pdwData ) & 0x0000FF00 ) >>  8L ) / 255.0f );
				fA = ( ( ( ( *pdwData ) & 0x000000FF ) >>  0L ) / 255.0f );

				// Convert data
				( *pdwData ) = ( ( ( DWORD )( fA * 0xFF ) ) << 24L ) |
								 ( ( ( DWORD )( fR * 0xFF ) ) << 16L ) |
								 ( ( ( DWORD )( fG * 0xFF ) ) <<  8L ) |
								 ( ( ( DWORD )( fB * 0xFF ) ) <<  0L);
				}
			}

	// Copy image data
	memcpy( rect.pBits, ( m_pImage + xprh.dwHeaderSize ), desc.Size );

	// Unlock rectangle
	pTempSurface->UnlockRect( );
	}

	// Success?
	if ( bSuccess )
		{
		// Create the texture
		bSuccess = SUCCEEDED( D3DXCreateTexture( pd3dDevice,
												 _XBE_TITLE_IMAGE_WIDTH,
												 _XBE_TITLE_IMAGE_HEIGHT, 
												 1,
												 0,
												 D3DFMT_DXT1,
												 D3DPOOL_DEFAULT,
												 &m_pTitleImageTexture ) );
		}

	// Success?
	if ( bSuccess )
		{
		// Add reference (no need. The reference count is initially 1)
//    m_pTitleImageTexture->AddRef( );

		// Get surface level
		m_pTitleImageTexture->GetSurfaceLevel( 0, &pDestSurface );

		// Load surface
		bSuccess = SUCCEEDED( D3DXLoadSurfaceFromSurface( pDestSurface,
														  NULL,
														  NULL,
														  pTempSurface,
														  NULL,
														  NULL,
														  D3DX_FILTER_NONE,
														  0 ) );

		// Release
		if ( pDestSurface )
			{
			pDestSurface->Release( );
			pDestSurface = NULL;
			}

		// Success?
		if ( !bSuccess )
			{
			// Release texture (if any)
			if ( m_pTitleImageTexture )
				{
				m_pTitleImageTexture->Release( );
				m_pTitleImageTexture = NULL;
				}
			}
		}

	// Release
	if ( pTempSurface )
		{
		pTempSurface->Release( );
		pTempSurface = NULL;
		}

	return ( bSuccess ? 1 : 0 );
}


int CXBExecutable::ReadAltTitleImage( void )
{
	char				* pszPos;

	LPDIRECT3DDEVICE8	  pd3dDevice;

	// Copy file name
	strcpy( m_szBuffer, m_szFileName );

	// Find last '\'
	if ( ( pszPos = ( char * )strrchr( m_szBuffer, ( int )'\\' ) ) == ( char * )NULL )
		{
		// Not found. Return not OK
		return 0;
		}

	// Copy reference
	pd3dDevice = TheseusGetD3DDev();

	// Release previous texture (if any)
	if ( m_pTitleImageTexture )
		{
		m_pTitleImageTexture->Release( );
		m_pTitleImageTexture = NULL;
		}

	// Terminate string
	*pszPos = ( char )'\0';

	// Add alternative image name
	strcat( m_szBuffer, "\\avalaunch_icon.png" );

	// Create texture and return status
	if ( SUCCEEDED( D3DXCreateTextureFromFile( pd3dDevice,
											   m_szBuffer,
											   &m_pTitleImageTexture ) ) )
		{
		// Return OK
		return 1;
		}

	// Terminate string
	*pszPos = ( char )'\0';

	// Add alternative image name
	strcat( m_szBuffer, "\\avalaunch_icon.jpg" );

	// Create texture and return status
	if ( SUCCEEDED( D3DXCreateTextureFromFile( pd3dDevice,
											   m_szBuffer,
											   &m_pTitleImageTexture ) ) )
		{
		// Return OK
		return 1;
		}

	// Copy file name
	strcpy( m_szBuffer, m_szFileName );

	// Find last '.'
	if ( ( pszPos = ( char * )strrchr( m_szBuffer, ( int )'.' ) ) == ( char * )NULL )
		{
		// Not found. Return not OK
		return 0;
		}

	// Terminate string
	*pszPos = ( char )'\0';

	// Add alternative image name
	strcat( m_szBuffer, ".png" );

	// Create texture and return status
	if ( SUCCEEDED( D3DXCreateTextureFromFile( pd3dDevice,
											   m_szBuffer,
											   &m_pTitleImageTexture ) ) )
		{
		// Return OK
		return 1;
		}

	// Terminate string
	*pszPos = ( char )'\0';

	// Add alternative image name
	strcat( m_szBuffer, ".jpg" );

	// Create texture and return status
	if ( SUCCEEDED( D3DXCreateTextureFromFile( pd3dDevice,
											   m_szBuffer,
											   &m_pTitleImageTexture ) ) )
		{
		// Return OK
		return 1;
		}

	// Return not OK
	return 0;
}


int CXBExecutable::ReadTitleName( void )
{
  HANDLE  hTitleMetaFile;

  DWORD   dwRead;

  WCHAR   wszTemp[ 1024 ];
  WCHAR * wpszPos;

  // Clear names
  memset( m_szTitleName, 0, sizeof( m_szTitleName ) );
  memset( wszTemp,       0, sizeof( wszTemp ) );

  // Do we have a title id.?
  if ( m_ulTitleId == ( unsigned long )0 )
    {
    // Return OK
    return 1;
    }

  // Compose file name
  sprintf( m_szBuffer, "E:\\UDATA\\%08lx\\TitleMeta.xbx", m_ulTitleId );

  // Open the local file
  hTitleMetaFile = CreateFile( m_szBuffer,
                               GENERIC_READ,
                               0,
                               NULL,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL );

  // Did we find the nick name file?
  if ( hTitleMetaFile == ( HANDLE )INVALID_HANDLE_VALUE )
    {
    // Unable to open file. Return OK
    return 1;
    }

  // Read title name (assume no more than 255 characters)
  ::ReadFile( hTitleMetaFile, wszTemp, sizeof( wszTemp ), &dwRead, NULL );

  // Find the string 'TitleName='
  if ( ( wpszPos = ( WCHAR * )wcsstr( wszTemp, L"TitleName=") ) != ( WCHAR * )NULL )
    {
    // Convert string
    strcpy( m_szTitleName, Trim( StrReplace( GetThinText( ( wpszPos + 10 ) ), ( char )'\r', ( char )'\0' ) ) );
    }

  // Close file
  CloseHandle( hTitleMetaFile );

  // Return OK
  return 1;
}

#define ARRAYSIZE(a) (sizeof(a) / sizeof(a[0]))
