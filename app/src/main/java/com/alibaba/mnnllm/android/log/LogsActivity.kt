package com.alibaba.mnnllm.android.log

import android.content.Intent
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.TextWatcher
import android.util.Log
import android.view.MenuItem
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.Toolbar
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.alibaba.mnnllm.android.R

class LogsActivity : AppCompatActivity() {

    private lateinit var adapter: LogsAdapter
    private lateinit var recycler: RecyclerView
    private lateinit var emptyView: TextView
    private lateinit var filterEdit: EditText
    private var minLevel: Int = Log.VERBOSE
    private val mainHandler = Handler(Looper.getMainLooper())
    private val refreshListener: () -> Unit = { refresh() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_logs)

        val toolbar: Toolbar = findViewById(R.id.toolbar)
        setSupportActionBar(toolbar)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.setTitle(R.string.logs_activity_title)

        recycler = findViewById(R.id.recycler_logs)
        emptyView = findViewById(R.id.tv_empty)
        filterEdit = findViewById(R.id.et_filter)

        recycler.layoutManager = LinearLayoutManager(this)
        adapter = LogsAdapter()
        recycler.adapter = adapter

        setupLevelSpinner()
        filterEdit.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) = refresh()
        })

        findViewById<View>(R.id.btn_copy_all).setOnClickListener {
            val text = AppLogger.copyAll(this)
            Toast.makeText(this, R.string.logs_copied, Toast.LENGTH_SHORT).show()
        }
        findViewById<View>(R.id.btn_share).setOnClickListener {
            val intent = AppLogger.buildShareIntent(this)
            if (intent != null) {
                try {
                    startActivity(Intent.createChooser(intent, getString(R.string.logs_share_chooser)))
                } catch (e: Exception) {
                    Toast.makeText(this, R.string.update_download_failed, Toast.LENGTH_SHORT).show()
                }
            }
        }
        findViewById<View>(R.id.btn_clear).setOnClickListener {
            AppLogger.clear()
            Toast.makeText(this, R.string.logs_cleared, Toast.LENGTH_SHORT).show()
        }
    }

    private fun setupLevelSpinner() {
        val labels = listOf(
            getString(R.string.logs_level_all),
            getString(R.string.logs_level_verbose),
            getString(R.string.logs_level_info),
            getString(R.string.logs_level_warn),
            getString(R.string.logs_level_error)
        )
        val spinner = findViewById<android.widget.Spinner>(R.id.spinner_level)
        val aa = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, labels)
        spinner.adapter = aa
        spinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                minLevel = when (position) {
                    1 -> Log.VERBOSE
                    2 -> Log.INFO
                    3 -> Log.WARN
                    4 -> Log.ERROR
                    else -> Log.VERBOSE
                }
                refresh()
            }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }
    }

    private fun refresh() {
        val keyword = filterEdit.text?.toString()?.trim()?.lowercase() ?: ""
        val filtered = AppLogger.snapshot().filter { e ->
            e.level >= minLevel &&
                (keyword.isEmpty() ||
                    e.tag.lowercase().contains(keyword) ||
                    e.message.lowercase().contains(keyword))
        }
        adapter.submit(filtered)
        emptyView.visibility = if (filtered.isEmpty()) View.VISIBLE else View.GONE
        if (filtered.isNotEmpty()) {
            recycler.scrollToPosition(filtered.size - 1)
        }
    }

    override fun onResume() {
        super.onResume()
        AppLogger.addListener(refreshListener)
        refresh()
    }

    override fun onPause() {
        super.onPause()
        AppLogger.removeListener(refreshListener)
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == android.R.id.home) {
            finish()
            return true
        }
        return super.onOptionsItemSelected(item)
    }
}
