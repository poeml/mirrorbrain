#!/usr/bin/env ruby


# this script generates a xml file from the 'redirect_stats'-table of the
# opensuse download redirector, and sends it to the opensuse build service api
# via http put request.


# <CONFIG>
#
db_host  = 'localhost'
db_port  = 3306
db_user  = 'root'
db_pass  = ''
db_name  = 'redirector'
db_table = 'redirect_stats'
#
# </CONFIG>


##########################################################################


require 'rubygems'
require_gem 'activesupport'
require_gem 'activerecord'


# connect to database
ActiveRecord::Base.establish_connection(
  :adapter  => 'mysql',
  :host     => db_host,
  :port     => db_port,
  :username => db_user,
  :password => db_pass,
  :database => db_name
)


# define model for statistics entries
class RedirectStats < ActiveRecord::Base ; end


# get all entries from database
db_stats = RedirectStats.find :all


# build nested hash with counters
stats = {}
db_stats.each do |s|
  stats[ s.project ] ||= {}
  stats[ s.project ][ s.package ] ||= {}
  stats[ s.project ][ s.package ][ s.repository ] ||= {}
  stats[ s.project ][ s.package ][ s.repository ][ s.arch ] ||= []
  stats[ s.project ][ s.package ][ s.repository ][ s.arch ] << s
end


# initialize xml-builder, send result to the xml_output variable
xml = Builder::XmlMarkup.new( :target => xml_output='', :indent => 2 )


# generate xml
xml.instruct!
xml.redirect_stats do
  stats.each_pair do |project_name, project|

    xml.project( :name => project_name ) do
      project.each_pair do |package_name, package|

        xml.package( :name => package_name ) do
          package.each_pair do |repo_name, repo|

            xml.repository( :name => repo_name ) do
              repo.each_pair do |arch_name, arch|

                xml.arch( :name => arch_name ) do
                  arch.each do |counter|

                    xml.count(
                      counter.count,
                      :filename   => counter.filename,
                      :filetype   => counter.filetype,
                      :version    => counter.version,
                      :release    => counter.release,
                      :created_at => counter.created_at.xmlschema,
                      :counted_at => counter.counted_at.xmlschema
                    )

                  end # each_counter

                end # 'arch' xml tag

              end # each_arch
            end # 'repository' xml tag

          end # each_repo
        end # 'package' xml tag

      end # each_package
    end # 'project' xml tag

  end # each_project
end # outer 'redirect_stats' xml tag




# for now, just print it on stdout
puts xml_output

