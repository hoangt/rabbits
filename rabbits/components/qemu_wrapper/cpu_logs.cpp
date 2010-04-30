/*
 *  Copyright (c) 2010 TIMA Laboratory
 *
 *  This file is part of Rabbits.
 *
 *  Rabbits is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Rabbits is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Rabbits.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cpu_logs.h>
#include <cfg.h>

//#define TIME_AT_FV_LOG_FILE
//#define TIME_AT_FV_LOG_GRF

#define FILE_TIME_AT_FV "logTimeAtFv"

static inline void writepipe (int &pipe, void* addr, int nbytes)
{
    static bool s_set_sigpipe_1 = false;
    if (!s_set_sigpipe_1)
    {
        signal (SIGPIPE, SIG_IGN);
        s_set_sigpipe_1 = true;
    }

    if (pipe != 0 && nbytes != ::write (pipe, addr, nbytes))
        pipe = 0;
}

cpu_logs::cpu_logs (int ncpu)
{
    m_ncpu = ncpu;

    InitInternal ();
}

cpu_logs::~cpu_logs ()
{
    if (m_ns_time_at_fv)
    {
        delete [] m_ns_time_at_fv;
        m_ns_time_at_fv = NULL;
    }

    if (m_ns_time_at_fv_prev)
    {
        delete [] m_ns_time_at_fv_prev;
        m_ns_time_at_fv_prev = NULL;
    }

    if (m_hword_ncycles)
    {
        delete [] m_hword_ncycles;
        m_hword_ncycles = NULL;
    }

#ifdef TIME_AT_FV_LOG_FILE
    if (file_fv)
    {
        fclose (file_fv);
        file_fv = NULL;
    }
#endif
}

void cpu_logs::InitInternal ()
{
    int					i, cpu;

    m_ns_time_at_fv = new unsigned long long [m_ncpu][NO_FV_LEVELS];
    m_ns_time_at_fv_prev = new unsigned long long [m_ncpu][NO_FV_LEVELS];
    m_hword_ncycles = new unsigned long [m_ncpu];
    for (cpu = 0; cpu < m_ncpu; cpu++)
    {
        for (i = 0; i < NO_FV_LEVELS; i++)
        {
            m_ns_time_at_fv[cpu][i] = 0;
            m_ns_time_at_fv_prev[cpu][i] = 0;
        }
        m_hword_ncycles[cpu] = (unsigned long) -1;
    }
    m_pipe_grf_run_at_fv = 0;
    m_pid_grf_run_at_fv = 0;
    file_fv = NULL;

#ifdef TIME_AT_FV_LOG_FILE
    //"time at fv" file
    if ((file_fv = fopen (FILE_TIME_AT_FV, "w")) == NULL)
    {
        printf ("Cannot open log %s", FILE_TIME_AT_FV);
        exit (1);
    }
    tm				ct;
    time_t			tt;
    time (&tt);
    localtime_r (&tt, &ct);
    fprintf (file_fv, "\nSimulation start time=\t%02d-%02d-%04d %02d:%02d:%02d\n",
             ct.tm_mday, ct.tm_mon + 1, ct.tm_year + 1900, ct.tm_hour, ct.tm_min, ct.tm_sec);
    fprintf (file_fv, 
             "                       \t           Fmax\t       Fmax*3/4\t"
             "         Fmax/2\t         Fmax/4\t              0\n");
#endif

#ifdef TIME_AT_FV_LOG_GRF
    //"time at fv" grf
    int					apipe[2] = {0, 0};
    char				*ps, s[100];
    unsigned long		val;
    long long			vall;

    pipe (apipe);
    if ((m_pid_grf_run_at_fv = fork()) == 0)
    {
        close (0);
        dup (apipe[0]);
        close (apipe[0]);
        close (apipe[1]);

        int			fdnull = open ("/dev/null", O_WRONLY);
        close (1);
        close (2);
        dup2 (1, fdnull);
        dup2 (2, fdnull);
        close (fdnull);

        if (execlp ("chronograph", "chronograph", NULL) == -1)
        {
            perror ("chronograph: execlp failed");
            exit (1);
        }
    }
    close (apipe[0]);
    m_pipe_grf_run_at_fv = apipe[1];

    // (4 (for string length) + (strlen + 1)) title of the graphs window
    ps = (char *) "Average frequency";
    val = strlen (ps) + 1;
    writepipe (m_pipe_grf_run_at_fv, &val, 4);
    writepipe (m_pipe_grf_run_at_fv, ps, val);

    val = m_ncpu | 0x80;
    // (4) the number of graphs (m_ncpu + the avg (MSb=1))
    writepipe (m_pipe_grf_run_at_fv, &val, 4);
    // (4) number of ms between 2 consecutive updates of the graphs
    val = 20;
    writepipe (m_pipe_grf_run_at_fv, &val, 4);

    for (i = 0; i < m_ncpu; i++)
    {
        //for each graph (except avg, if the case)
        // (4) number of curves in the crt graph
        val = 1;
        writepipe (m_pipe_grf_run_at_fv, &val, 4);
        // (8) minimum value possible in the crt graph
        vall = 0;
        writepipe (m_pipe_grf_run_at_fv, &vall, 8);
        // (8) maximum value possible in the crt graph
        vall = 100;
        writepipe (m_pipe_grf_run_at_fv, &vall, 8);
    }

    for (i = 0; i  <= m_ncpu; i++)
    {
        //for each graph (including the avg, if the case)
        if (i == m_ncpu)
            strcpy (s, "All CPUs (%)");
        else
            sprintf (s, "CPU %4d (%%)", i);
        val = strlen (s) + 1;
        // (4 + strlen  + 1) name of the graph (including measuring units)
        writepipe (m_pipe_grf_run_at_fv, &val, 4);
        writepipe (m_pipe_grf_run_at_fv, s, val);
    }
#endif
}

void cpu_logs::AddTimeAtFv (int cpu, int fvlevel, unsigned long long time)
{
    m_ns_time_at_fv[cpu][fvlevel] += time;
}

unsigned long cpu_logs::get_cpu_ncycles (unsigned long cpu)
{
    unsigned long		ret;
    if (m_hword_ncycles[cpu] == (unsigned long)-1)
    {
        unsigned long long	sum = 0;
        for (int i = 0; i < NO_FV_LEVELS; i++)
            sum += (unsigned long long) (m_ns_time_at_fv[cpu][i] * g_cpu_fv_percents[i] *
                                         ((double) SYSTEM_CLOCK_FV / 1000000000));
        ret = sum & 0xFFFFFFFF;
        m_hword_ncycles[cpu] = sum >> 32;
    }
    else
    {
        ret = m_hword_ncycles[cpu];
        m_hword_ncycles[cpu] = (unsigned long)-1;
    }

    return ret;
}

void cpu_logs::UpdateFvGrf ()
{
    return;

    static int				cnt = 0;	
    if (++cnt < m_ncpu)
        return;
    cnt = 0;

    int						i, cpu;

#ifdef TIME_AT_FV_LOG_FILE
    //"time at fv" file
    static int			cnt1 = 0;
    if (++cnt1 == 100)
    {
        cnt1 = 0;

        fprintf (file_fv, "time(us) = %llu\n", sc_time_stamp ().value () / 1000000);
        for (cpu = 0; cpu < m_ncpu; cpu++)
        {
            fprintf (file_fv, "CPU %2d          \t", cpu);
            for (i = 0; i < NO_FV_LEVELS; i++)
                fprintf (file_fv, "%15llu\t", m_ns_time_at_fv[cpu][NO_FV_LEVELS - 1 - i] / 1000);
            fprintf (file_fv, "\n");
        }
    }	
#endif

#ifdef TIME_AT_FV_LOG_GRF
    //"time at fv" grf
    static int			cnt2 = 0;
    if (++cnt2 == 2)
    {
        cnt2 = 0;

        double					s1, s2, diff;
        unsigned long			val = 0;
        for (cpu = m_ncpu - 1; cpu >= 0; cpu--)
        {
            s1 = 0;
            s2 = 0;

            for (i = 0; i < NO_FV_LEVELS; i++)
            {
                diff = m_ns_time_at_fv[cpu][i] - m_ns_time_at_fv_prev[cpu][i];
                s1 += diff * g_cpu_fv_percents[i];
                s2 += diff;
            }

            if (s2)
                val = (val << 8) + (unsigned long) (s1 / s2);
        }
        writepipe (m_pipe_grf_run_at_fv, &val, m_ncpu);
        memcpy (m_ns_time_at_fv_prev, m_ns_time_at_fv, m_ncpu * NO_FV_LEVELS * sizeof (unsigned long long));
    }
#endif
}

/*
 * Vim standard variables
 * vim:set ts=4 expandtab tw=80 cindent syntax=c:
 *
 * Emacs standard variables
 * Local Variables:
 * mode: c
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
