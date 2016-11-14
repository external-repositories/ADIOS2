/*
 * CBZIP2.h
 *
 *  Created on: Oct 17, 2016
 *      Author: wfg
 */

#ifndef CBZIP2_H_
#define CBZIP2_H_


#include <bzip2.h> //bzip2 library header


#include "core/CTransform.h"


namespace adios
{


class CBZIP2 : public CTransform
{

public:

    /**
     * Initialize parent method
     * @param compressionLevel
     * @param variable
     */
    CBZIP2( );

    ~CBZIP2( );

    void Compress( const SVariable<char>& variableIn, SVariable<char>& variableOut, const unsigned int level  ) const;
    void Compress( const SVariable<unsigned char>& variableIn, SVariable<unsigned char>& variableOut, const unsigned int level  ) const;
    void Compress( const SVariable<short>& variableIn, SVariable<short>& variableOut, const unsigned int level  ) const;
    void Compress( const SVariable<unsigned short>& variableIn, SVariable<unsigned short>& variableOut, const unsigned int level  ) const;
    void Compress( const SVariable<int>& variableIn, SVariable<int>& variableOut, const unsigned int level  ) const;
    void Compress( const SVariable<unsigned int>& variableIn, SVariable<unsigned int>& variableOut, const unsigned int level  ) const;
    void Compress( const SVariable<long int>& variableIn, SVariable<long int>& variableOut, const unsigned int level  ) const;
    void Compress( const SVariable<unsigned long int>& variableIn, SVariable<unsigned long int>& variableOut, const unsigned int level  ) const;
    void Compress( const SVariable<long long int>& variableIn, SVariable<long long int>& variableOut, const unsigned int level  ) const;
    void Compress( const SVariable<unsigned long long int>& variableIn, SVariable<unsigned long long int>& variableOut, const unsigned int level  ) const;
    void Compress( const SVariable<float>& variableIn, SVariable<float>& variableOut, const unsigned int level  ) const;
    void Compress( const SVariable<double>& variableIn, SVariable<double>& variableOut, const unsigned int level  ) const;
    void Compress( const SVariable<long double>& variableIn, SVariable<long double>& variableOut, const unsigned int level  ) const;

    void Decompress( const SVariable<char>& variableIn, SVariable<char>& variableOut, const unsigned int level  ) const;
    void Decompress( const SVariable<unsigned char>& variableIn, SVariable<unsigned char>& variableOut, const unsigned int level  ) const;
    void Decompress( const SVariable<short>& variableIn, SVariable<short>& variableOut, const unsigned int level  ) const;
    void Decompress( const SVariable<unsigned short>& variableIn, SVariable<unsigned short>& variableOut, const unsigned int level  ) const;
    void Decompress( const SVariable<int>& variableIn, SVariable<int>& variableOut, const unsigned int level  ) const;
    void Decompress( const SVariable<unsigned int>& variableIn, SVariable<unsigned int>& variableOut, const unsigned int level  ) const;
    void Decompress( const SVariable<long int>& variableIn, SVariable<long int>& variableOut, const unsigned int level  ) const;
    void Decompress( const SVariable<unsigned long int>& variableIn, SVariable<unsigned long int>& variableOut, const unsigned int level  ) const;
    void Decompress( const SVariable<long long int>& variableIn, SVariable<long long int>& variableOut, const unsigned int level  ) const;
    void Decompress( const SVariable<unsigned long long int>& variableIn, SVariable<unsigned long long int>& variableOut, const unsigned int level  ) const;
    void Decompress( const SVariable<float>& variableIn, SVariable<float>& variableOut, const unsigned int level  ) const;
    void Decompress( const SVariable<double>& variableIn, SVariable<double>& variableOut, const unsigned int level  ) const;
    void Decompress( const SVariable<long double>& variableIn, SVariable<long double>& variableOut, const unsigned int level  ) const;
};


} //end namespace



#endif /* CBZIP2_H_ */
