/******************************************************
** See the accompanying README and LICENSE files
** Author(s): Jeremy Magland
** Created: 5/27/2016
*******************************************************/

#include "multiscaletimeseries.h"
#include "cachemanager.h"
#include "msmisc.h"

#include <QFile>
#include <diskwritemda.h>
#include <taskprogress.h>
#include <sys/stat.h>
#include <QFileInfo>
#include <QMutex>
#include <math.h>
#include "mountainprocessrunner.h"
#include "mlutils.h"

class MultiScaleTimeSeriesPrivate {
public:
    MultiScaleTimeSeries* q;

    DiskReadMda m_data;
    DiskReadMda m_multiscale_data;
    bool m_initialized;
    QString m_remote_data_type;
    QString m_mlproxy_url;

    QString get_multiscale_fname();
    bool get_data(Mda& min, Mda& max, long t1, long t2, long ds_factor);

    static bool is_power_of_3(long N);
};

MultiScaleTimeSeries::MultiScaleTimeSeries()
{
    d = new MultiScaleTimeSeriesPrivate;
    d->q = this;
    d->m_initialized = false;
    d->m_remote_data_type = "float32";
}

MultiScaleTimeSeries::MultiScaleTimeSeries(const MultiScaleTimeSeries& other)
{
    d = new MultiScaleTimeSeriesPrivate;
    d->q = this;

    d->m_data = other.d->m_data;
    d->m_multiscale_data = other.d->m_multiscale_data;
    d->m_initialized = other.d->m_initialized;
    d->m_mlproxy_url = other.d->m_mlproxy_url;
    d->m_remote_data_type = other.d->m_remote_data_type;
}

MultiScaleTimeSeries::~MultiScaleTimeSeries()
{
    delete d;
}

void MultiScaleTimeSeries::operator=(const MultiScaleTimeSeries& other)
{
    d->m_data = other.d->m_data;
    d->m_multiscale_data = other.d->m_multiscale_data;
    d->m_initialized = other.d->m_initialized;
    d->m_mlproxy_url = other.d->m_mlproxy_url;
    d->m_remote_data_type = other.d->m_remote_data_type;
}

void MultiScaleTimeSeries::setData(const DiskReadMda& X)
{
    d->m_data = X;
    d->m_multiscale_data = DiskReadMda();
    d->m_initialized = false;
}

void MultiScaleTimeSeries::setMLProxyUrl(const QString& url)
{
    d->m_mlproxy_url = url;
}

void MultiScaleTimeSeries::initialize()
{
    TaskProgress task("Initializing multiscaletimeseries");
    QString path;
    {
        path = d->m_data.makePath();
        if (path.isEmpty()) {
            qWarning() << "Unable to initialize multiscaletimeseries.... path is empty.";
            d->m_initialized = true;
            return;
        }
    }

    MountainProcessRunner MPR;
    QString path_out;
    {
        MPR.setProcessorName("create_multiscale_timeseries");
        QVariantMap params;
        params["timeseries"] = path;
        MPR.setInputParameters(params);
        MPR.setMLProxyUrl(d->m_mlproxy_url);
        path_out = MPR.makeOutputFilePath("timeseries_out");
        MPR.setDetach(true);
        task.log("Running process");
    }
    MPR.runProcess();
    {
        d->m_multiscale_data.setPath(path_out);
        task.log(d->m_data.makePath());
        task.log(d->m_multiscale_data.makePath());
        task.log(QString("%1x%2 -- %3x%4").arg(d->m_multiscale_data.N1()).arg(d->m_multiscale_data.N2()).arg(d->m_data.N1()).arg(d->m_data.N2()));
        d->m_initialized = true;
    }
}

long MultiScaleTimeSeries::N1()
{
    return d->m_data.N1();
}

long MultiScaleTimeSeries::N2()
{
    return d->m_data.N2();
}

bool MultiScaleTimeSeries::getData(Mda& min, Mda& max, long t1, long t2, long ds_factor)
{
    return d->get_data(min, max, t1, t2, ds_factor);
}

double MultiScaleTimeSeries::minimum()
{
    long ds_factor = MultiScaleTimeSeries::smallest_power_of_3_larger_than(this->N2() / 3);
    Mda min, max;
    this->getData(min, max, 0, 0, ds_factor);
    return min.minimum();
}

double MultiScaleTimeSeries::maximum()
{
    long ds_factor = MultiScaleTimeSeries::smallest_power_of_3_larger_than(this->N2() / 3);
    Mda min, max;
    this->getData(min, max, 0, 0, ds_factor);
    return max.maximum();
}

long MultiScaleTimeSeries::smallest_power_of_3_larger_than(long N)
{
    long ret = 1;
    while (ret < N) {
        ret *= 3;
    }
    return ret;
}

bool MultiScaleTimeSeriesPrivate::get_data(Mda& min, Mda& max, long t1, long t2, long ds_factor)
{
    long M, N, N2;
    {
        if (!m_initialized) {
            qWarning() << "Cannot get_data. Multiscale timeseries is not initialized!";
            return false;
        }
        M = m_data.N1();
        N2 = m_data.N2();
        N = MultiScaleTimeSeries::smallest_power_of_3_larger_than(N2);

        if ((t2 < 0) || (t1 >= m_data.N2() / ds_factor)) {
            //we are completely out of range, so we return all zeros
            min.allocate(M, (t2 - t1 + 1));
            max.allocate(M, (t2 - t1 + 1));
            return true;
        }
    }

    if ((t1 < 0) || (t2 >= N2 / ds_factor)) {
        //we are somewhat out of range.
        min.allocate(M, t2 - t1 + 1);
        max.allocate(M, t2 - t1 + 1);
        Mda min0, max0;
        long s1 = t1, s2 = t2;
        if (s1 < 0)
            s1 = 0;
        if (s2 >= N2 / ds_factor)
            s2 = N2 / ds_factor - 1;
        if (!get_data(min0, max0, s1, s2, ds_factor)) {
            return false;
        }
        if (t1 >= 0) {
            min.setChunk(min0, 0, 0);
            max.setChunk(max0, 0, 0);
        } else {
            min.setChunk(min0, 0, -t1);
            max.setChunk(max0, 0, -t1);
        }
        return true;
    }

    if (ds_factor == 1) {
        m_data.readChunk(min, 0, t1, M, t2 - t1 + 1);
        max = min;
        return true;
    }

    {
        if (!is_power_of_3(ds_factor)) {
            qWarning() << "Invalid ds_factor: " + ds_factor;
            return false;
        }

        m_multiscale_data.setRemoteDataType(m_remote_data_type);

        long t_offset_min = 0;
        long ds_factor_0 = 3;
        while (ds_factor_0 < ds_factor) {
            t_offset_min += 2 * (N / ds_factor_0);
            ds_factor_0 *= 3;
        }
        long t_offset_max = t_offset_min + N / ds_factor;

        m_multiscale_data.readChunk(min, 0, t1 + t_offset_min, M, t2 - t1 + 1);
        m_multiscale_data.readChunk(max, 0, t1 + t_offset_max, M, t2 - t1 + 1);

        if (thread_interrupt_requested()) {
            return false;
        }
    }

    return true;
}

bool MultiScaleTimeSeriesPrivate::is_power_of_3(long N)
{
    double val = N;
    while (val > 1) {
        val /= 3;
    }
    return (val == 1);
}
