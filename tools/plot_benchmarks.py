"""Regenerate compact README plots from measured samples, never invented data."""
import json
from pathlib import Path
import statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import FixedLocator, FuncFormatter

ROOT = Path(__file__).resolve().parents[1]
data = json.loads((ROOT/'docs/benchmarks.json').read_text())
plt.rcParams.update({'font.family':'DejaVu Sans', 'font.size':11, 'axes.spines.top':False,
    'axes.spines.right':False, 'axes.spines.left':False, 'figure.facecolor':'#111521',
    'axes.facecolor':'#111521', 'text.color':'#edf0fa', 'axes.labelcolor':'#c9cee0',
    'xtick.color':'#aeb7d1', 'ytick.color':'#edf0fa', 'axes.edgecolor':'#535e78',
    'savefig.facecolor':'#111521'})
phone = data['phone_headless']['results']


def chart(rows, title, subtitle, filename, logarithmic=False):
    fig, ax = plt.subplots(figsize=(10,4.6))
    fig.subplots_adjust(left=.25,right=.86,top=.69,bottom=.25)
    fig.text(.045,.91,title,fontsize=21,weight='bold')
    fig.text(.045,.81,subtitle,fontsize=11,color='#b3bdd4')
    for i,(label,samples,color) in enumerate(rows):
        median = statistics.median(samples)
        ax.barh(i,median,color=color,height=.49,zorder=2)
        offsets=[-.13,-.08,-.025,.025,.08,.13]
        ax.scatter(samples,[i+x for x in offsets],s=15,c='white',alpha=.7,zorder=3,linewidths=0)
        ax.annotate(f'{median:,.0f} ms',xy=(median,i),xytext=(10,0),textcoords='offset points',va='center',weight='bold',fontsize=12)
    ax.set_yticks(range(len(rows)),[r[0] for r in rows]);ax.invert_yaxis()
    ax.tick_params(axis='y',length=0,pad=15)
    if logarithmic:
        ax.set_xscale('log');ax.set_xlim(10,4500)
        ax.xaxis.set_major_locator(FixedLocator([10,100,1000]))
        ax.xaxis.set_major_formatter(FuncFormatter(lambda v,p:f'{v:,.0f}'))
        ax.set_xlabel('Complete model call · milliseconds · logarithmic axis · lower is better',labelpad=12,fontsize=9)
    else:
        ax.set_xlim(0,2900);ax.set_xlabel('Complete model call · milliseconds · lower is better',labelpad=12,fontsize=10)
    ax.xaxis.grid(True,alpha=.13,zorder=0);ax.set_axisbelow(True)
    fig.text(.045,.08,'320×144 input · 320×320 canvas · one pass · history reset · six warm calls per run',fontsize=9,color='#aeb7d1')
    fig.text(.045,.035,'Dots = measured calls. First call excluded. 15 Oct 2026 · Phoenix 0.8.1-beta · methods in docs/BENCHMARKS.md',fontsize=8,color='#818eac')
    fig.savefig(ROOT/'docs'/filename,dpi=150)
    plt.close(fig)


chart([('Adreno 735\nbefore',phone['baseline']['warm_call_ms'],'#db826b'),
       ('Adreno 735\noptimized',phone['optimized']['warm_call_ms'],'#9c82f4')],
      '1.61× faster. Still a slideshow.', 'Same phone and input. Identical output bytes. Smaller is better for once.', 'phone-optimization.png')
chart([('Phone · Adreno 735',phone['optimized']['warm_call_ms'],'#9c82f4'),
       ('PC · RTX 5080',data['pc_headless']['warm_call_ms'],'#5acdb3')],
      'Pocket computer meets actual computer.', 'Same workload and C API boundary; device-specific Vulkan kernels. Not display or game FPS.', 'phone-vs-pc.png',True)
