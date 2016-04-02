#include "diskreadmda.h"
#include <stdio.h>
#include "mdaclient.h"
#include "mdaio.h"
#include <math.h>
#include <QUrl>

#define MAX_PATH_LEN 10000
#define DEFAULT_CHUNK_SIZE 10e6

class DiskReadMdaPrivate
{
public:
	DiskReadMda *q;
	FILE *m_file;
	bool m_file_open_failed;
	MDAIO_HEADER m_header;
    long m_mda_header_total_size;
	Mda m_internal_chunk;
	long m_current_internal_chunk_index;
	Mda m_memory_mda;
	bool m_use_memory_mda;
    bool m_use_mda_client;
    MdaClient m_mda_client;

	char m_path[MAX_PATH_LEN];
	void do_construct();
	bool open_file_if_needed();
	void copy_from(const DiskReadMda &other);
    long total_size();
};

DiskReadMda::DiskReadMda(const QString &path_or_url) {
	d=new DiskReadMdaPrivate;
	d->q=this;
	d->do_construct();
	if (!path_or_url.isEmpty()) {
		if (path_or_url.startsWith("http")) {
			this->setUrl(QUrl(path_or_url));
		}
		else {
			this->setPath(path_or_url);
		}
	}
}

DiskReadMda::DiskReadMda(const DiskReadMda &other)
{
	d=new DiskReadMdaPrivate;
	d->q=this;
	d->do_construct();
	d->copy_from(other);
}

DiskReadMda::DiskReadMda(const Mda &X)
{
	d=new DiskReadMdaPrivate;
	d->q=this;
	d->do_construct();
	d->m_use_memory_mda=true;
    d->m_memory_mda=X;
}

DiskReadMda::DiskReadMda(const QUrl &url)
{
    d=new DiskReadMdaPrivate;
    d->q=this;
    d->do_construct();
    d->m_use_mda_client=true;
    d->m_mda_client.setUrl(url.toString());
}

DiskReadMda::~DiskReadMda() {
	if (d->m_file) fclose(d->m_file);
	delete d;
}

void DiskReadMda::operator=(const DiskReadMda &other)
{
	d->copy_from(other);
}

void DiskReadMda::setPath(const QString &file_path_or_url)
{
	setPath(file_path_or_url.toLatin1().data());
}

void DiskReadMda::setPath(const char *file_path_or_url)
{
	if (QString(file_path_or_url).startsWith("http")) {
		this->setUrl(QUrl(file_path_or_url));
		return;
	}
	strcpy(d->m_path,file_path_or_url);
	if (d->m_file) {
		fclose(d->m_file);
		d->m_file=0;
	}
	d->m_use_mda_client=false;
	d->m_use_memory_mda=false;
	d->m_memory_mda.allocate(1,1);
	d->m_current_internal_chunk_index=-1;
}

void DiskReadMda::setUrl(const QUrl &url)
{
	if (d->m_file) {
		fclose(d->m_file);
		d->m_file=0;
	}

	d->m_use_mda_client=true;
	d->m_mda_client.setUrl(url.toString());

	d->m_use_memory_mda=false;
	d->m_memory_mda.allocate(1,1);

	d->m_current_internal_chunk_index=-1;
}

long DiskReadMda::N1() const
{
	if (d->m_use_memory_mda) return d->m_memory_mda.N1();
	if (d->m_use_mda_client) {
		qDebug() << __FUNCTION__  << __FILE__ << __LINE__ << d->m_mda_client.url();
		return d->m_mda_client.N1();
	}
	if (!d->open_file_if_needed()) return 0;
	return d->m_header.dims[0];
}

long DiskReadMda::N2() const
{
	if (d->m_use_memory_mda) return d->m_memory_mda.N2();
    if (d->m_use_mda_client) return d->m_mda_client.N2();
	if (!d->open_file_if_needed()) return 0;
	return d->m_header.dims[1];
}

long DiskReadMda::N3() const
{
	if (d->m_use_memory_mda) return d->m_memory_mda.N3();
    if (d->m_use_mda_client) return d->m_mda_client.N3();
	if (!d->open_file_if_needed()) return 0;
	return d->m_header.dims[2];
}

long DiskReadMda::N4() const
{
	if (d->m_use_memory_mda) return d->m_memory_mda.N4();
    if (d->m_use_mda_client) return 1;
	if (!d->open_file_if_needed()) return 0;
	return d->m_header.dims[3];
}

long DiskReadMda::N5() const
{
	if (d->m_use_memory_mda) return d->m_memory_mda.N5();
    if (d->m_use_mda_client) return 1;
	if (!d->open_file_if_needed()) return 0;
	return d->m_header.dims[4];
}

long DiskReadMda::N6() const
{
	if (d->m_use_memory_mda) return d->m_memory_mda.N6();
    if (d->m_use_mda_client) return 1;
	if (!d->open_file_if_needed()) return 0;
	return d->m_header.dims[5];
}

long DiskReadMda::totalSize() const
{
    return d->total_size();
}

bool DiskReadMda::readChunk(Mda &X, long i, long size) const
{
	if (d->m_use_memory_mda) {
		d->m_memory_mda.getChunk(X,i,size);
		return true;
	}
    if (d->m_use_mda_client) {
        ChunkParams params;
        params.i1=i; params.s1=size;
        params.i2=0; params.s2=0;
        params.i3=0; params.s3=0;
        X=d->m_mda_client.getChunk(params);
        return true;
    }
	if (!d->open_file_if_needed()) return false;
    X.allocate(size,1);
	long jA=qMax(i,0L);
    long jB=qMin(i+size-1,d->total_size()-1);
	long size_to_read=jB-jA+1;
	if (size_to_read>0) {
		fseek(d->m_file,d->m_header.header_size+d->m_header.num_bytes_per_entry*(jA),SEEK_SET);
		long bytes_read=mda_read_float64(&X.dataPtr()[jA-i],&d->m_header,size_to_read,d->m_file);
		if (bytes_read!=size_to_read) {
			printf("Warning problem reading chunk in diskreadmda: %ld<>%ld\n",bytes_read,size_to_read);
			return false;
		}
	}
	return true;
}

bool DiskReadMda::readChunk(Mda &X, long i1, long i2, long size1, long size2) const
{
    if (size2==0) {
        return readChunk(X,i1,size1);
    }
	if (d->m_use_memory_mda) {
		d->m_memory_mda.getChunk(X,i1,i2,size1,size2);
		return true;
	}
    if (d->m_use_mda_client) {
        ChunkParams params;
        params.i1=i1; params.s1=size1;
        params.i2=i2; params.s2=size2;
        params.i3=0; params.s3=0;
        X=d->m_mda_client.getChunk(params);
        return true;
    }
	if (!d->open_file_if_needed()) return false;
	if (size1==N1()) {
		//easy case
		X.allocate(size1,size2);
		long jA=qMax(i2,0L);
		long jB=qMin(i2+size2-1,N2()-1);
		long size2_to_read=jB-jA+1;
		if (size2_to_read>0) {
            fseek(d->m_file,d->m_header.header_size+d->m_header.num_bytes_per_entry*(i1+N1()*jA),SEEK_SET);
			long bytes_read=mda_read_float64(&X.dataPtr()[(jA-i2)*size1],&d->m_header,size1*size2_to_read,d->m_file);
			if (bytes_read!=size1*size2_to_read) {
				printf("Warning problem reading 2d chunk in diskreadmda: %ld<>%ld\n",bytes_read,size1*size2);
				return false;
            }
		}
		return true;
	}
	else {
		printf("Warning: This case not yet supported (diskreadmda::readchunk 2d).\n");
		return false;
	}
}

bool DiskReadMda::readChunk(Mda &X, long i1, long i2, long i3, long size1, long size2, long size3) const
{
    if (size3==0) {
        if (size2==0) {
            return readChunk(X,i1,size1);
        }
        else {
            return readChunk(X,i1,i2,size1,size2);
        }
    }
	if (d->m_use_memory_mda) {
		d->m_memory_mda.getChunk(X,i1,i2,i3,size1,size2,size3);
		return true;
	}
    if (d->m_use_mda_client) {
        ChunkParams params;
        params.i1=i1; params.s1=size1;
        params.i2=i2; params.s2=size2;
        params.i3=i3; params.s3=size3;
        X=d->m_mda_client.getChunk(params);
        return true;
    }
	if (!d->open_file_if_needed()) return false;
	if ((size1==N1())&&(size2==N2())) {
		//easy case
		X.allocate(size1,size2,size3);
		long jA=qMax(i3,0L);
		long jB=qMin(i3+size3-1,N3()-1);
		long size3_to_read=jB-jA+1;
		if (size3_to_read>0) {
			fseek(d->m_file,d->m_header.header_size+d->m_header.num_bytes_per_entry*(i1+N1()*i2+N1()*N2()*jA),SEEK_SET);
			long bytes_read=mda_read_float64(&X.dataPtr()[(jA-i3)*size1*size2],&d->m_header,size1*size2*size3_to_read,d->m_file);
			if (bytes_read!=size1*size2*size3_to_read) {
				printf("Warning problem reading 3d chunk in diskreadmda: %ld<>%ld\n",bytes_read,size1*size2*size3_to_read);
				return false;
			}
		}
		return true;
	}
	else {
		printf("Warning: This case not yet supported (diskreadmda::readchunk 3d).\n");
		return false;
	}
}

double DiskReadMda::value(long i) const
{
	if (d->m_use_memory_mda) return d->m_memory_mda.value(i);
    if ((i<0)||(i>=d->total_size())) return 0;
	long chunk_index=i/DEFAULT_CHUNK_SIZE;
	long offset=i-DEFAULT_CHUNK_SIZE*chunk_index;
	if (d->m_current_internal_chunk_index!=chunk_index) {
		long size_to_read=DEFAULT_CHUNK_SIZE;
        if (chunk_index*DEFAULT_CHUNK_SIZE+size_to_read>d->total_size()) size_to_read=d->total_size()-chunk_index*DEFAULT_CHUNK_SIZE;
		if (size_to_read) {
			this->readChunk(d->m_internal_chunk,chunk_index*DEFAULT_CHUNK_SIZE,size_to_read);
		}
		d->m_current_internal_chunk_index=chunk_index;
	}
	return d->m_internal_chunk.value(offset);
}

double DiskReadMda::value(long i1, long i2) const
{
	if (d->m_use_memory_mda) return d->m_memory_mda.value(i1,i2);
	if ((i1<0)||(i1>=N1())) return 0;
	if ((i2<0)||(i2>=N2())) return 0;
	return value(i1+N1()*i2);
}

double DiskReadMda::value(long i1, long i2, long i3) const
{
	if (d->m_use_memory_mda) return d->m_memory_mda.value(i1,i2,i3);
	if ((i1<0)||(i1>=N1())) return 0;
	if ((i2<0)||(i2>=N2())) return 0;
	if ((i3<0)||(i3>=N3())) return 0;
	return value(i1+N1()*i2+N1()*N2()*i3);
}

void DiskReadMdaPrivate::do_construct()
{
	strcpy(m_path,"");
	m_file_open_failed=false;
	m_file=0;
	m_current_internal_chunk_index=-1;
	m_use_memory_mda=false;
    m_use_mda_client=false;
}

bool DiskReadMdaPrivate::open_file_if_needed()
{
    if (m_use_mda_client) return true;
    if (m_use_memory_mda) return true;
	if (m_file) return true;
	if (m_file_open_failed) return false;
	m_file=fopen(m_path,"rb");
	if (m_file) {
		mda_read_header(&m_header,m_file);
        m_mda_header_total_size=1;
        for (int i=0; i<MDAIO_MAX_DIMS; i++) m_mda_header_total_size*=m_header.dims[i];
	}
	else {
		printf("Failed to open diskreadmda file: %s\n",m_path);
		m_file_open_failed=true;
		return false;
	}
	return true;
}

void DiskReadMdaPrivate::copy_from(const DiskReadMda &other)
{
	if (other.d->m_use_memory_mda) {
		this->m_use_memory_mda=true;
        this->m_use_mda_client=false;
		this->m_memory_mda=other.d->m_memory_mda;
		return;
	}
    if (other.d->m_use_mda_client) {
		q->setUrl(QUrl(other.d->m_mda_client.url()));
		return;
    }
    q->setPath(other.d->m_path);
}

long DiskReadMdaPrivate::total_size()
{
    if (m_use_memory_mda) return m_memory_mda.totalSize();
    if (m_use_mda_client) return m_mda_client.totalSize();
    return m_mda_header_total_size;
}

void diskreadmda_unit_test()
{
	printf("diskreadmda_unit_test...\n");

	int N1=20;
	int N2=20;
	int N3=20;

	Mda X,Y;
	X.allocate(N1,N2,N3);
	double sum1=0;
	for (int i3=0; i3<N3; i3++) {
		for (int i2=0; i2<N2; i2++) {
			for (int i1=0; i1<N1; i1++) {
				double val=sin(i1+sin(i2)+cos(i3));
				sum1+=val;
				X.setValue(val,i1,i2,i3);
			}
		}
	}

	double sum2=0;
	for (int i3=0; i3<N3; i3++) {
		for (int i2=0; i2<N2; i2++) {
			for (int i1=0; i1<N1; i1++) {
				double val=X.value(i1,i2,i3);
				sum2+=val;
			}
		}
	}

	X.write64("tmp_64.mda");
	Y.read("tmp_64.mda");
	double sum3=0;
	for (int i3=0; i3<N3; i3++) {
		for (int i2=0; i2<N2; i2++) {
			for (int i1=0; i1<N1; i1++) {
				double val=Y.value(i1,i2,i3);
				sum3+=val;
			}
		}
	}

	printf("The following should match:\n");
	printf("%.20f\n",sum1);
	printf("%.20f\n",sum2);
	printf("%.20f\n",sum3);

	X.write32("tmp_32.mda");
	Y.read("tmp_32.mda");
	double sum4=0;
	for (int i3=0; i3<N3; i3++) {
		for (int i2=0; i2<N2; i2++) {
			for (int i1=0; i1<N1; i1++) {
				double val=Y.value(i1,i2,i3);
				sum4+=val;
			}
		}
	}

	printf("The following should almost match up to 6 or so digits:\n");
	printf("%.20f\n",sum4);

	DiskReadMda Z;
	Z.setPath("tmp_64.mda");
	double sum5=0;
	for (int i3=0; i3<N3; i3++) {
		for (int i2=0; i2<N2; i2++) {
			for (int i1=0; i1<N1; i1++) {
				double val=Z.value(i1,i2,i3);
				sum5+=val;
			}
		}
	}
	printf("The following should match (from diskreadmda):\n");
	printf("%.20f\n",sum5);
}
