package com.alibaba.mnnllm.android.log

import android.graphics.Color
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import com.alibaba.mnnllm.android.R
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class LogsAdapter : RecyclerView.Adapter<LogsAdapter.VH>() {

    private val fmt = SimpleDateFormat("MM-dd HH:mm:ss.SSS", Locale.US)
    private val items = mutableListOf<LogEntry>()

    fun submit(list: List<LogEntry>) {
        items.clear()
        items.addAll(list)
        notifyDataSetChanged()
    }

    inner class VH(view: View) : RecyclerView.ViewHolder(view) {
        val header: TextView = view.findViewById(R.id.tv_log_header)
        val message: TextView = view.findViewById(R.id.tv_log_message)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val v = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_app_log, parent, false)
        return VH(v)
    }

    override fun getItemCount(): Int = items.size

    override fun onBindViewHolder(holder: VH, position: Int) {
        val e = items[position]
        val levelChar = when (e.level) {
            Log.VERBOSE -> "V"
            Log.DEBUG -> "D"
            Log.INFO -> "I"
            Log.WARN -> "W"
            Log.ERROR -> "E"
            else -> "A"
        }
        holder.header.text = "${fmt.format(Date(e.time))} $levelChar/${e.tag}"
        holder.header.setTextColor(colorFor(e.level))
        holder.message.text = e.message
    }

    private fun colorFor(level: Int): Int = when (level) {
        Log.ERROR -> Color.parseColor("#D32F2F")
        Log.WARN -> Color.parseColor("#E65100")
        Log.INFO -> Color.parseColor("#2E7D32")
        Log.DEBUG -> Color.parseColor("#5F6368")
        else -> Color.parseColor("#80868B")
    }
}
